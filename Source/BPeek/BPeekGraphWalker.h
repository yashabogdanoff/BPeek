#pragma once
#include "CoreMinimal.h"
#include "BPeekMarkdownWriter.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphNode_Comment.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_Switch.h"
#include "K2Node_MacroInstance.h"

/**
 * Coverage metrics aggregated per-BP. Used to report which percentage
 * of the blueprint's graph actually makes it into the markdown output.
 * Categories are mutually exclusive and sum to TotalGraphNodes.
 */
struct FBPeekCoverageStats
{
    int32 Blueprints          = 0;
    int32 TotalGraphNodes     = 0;

    int32 ReachableExecNodes  = 0;
    int32 PureDataNodes       = 0;
    int32 OrphanExecNodes     = 0;
    int32 CommentNodes        = 0;
    int32 LocalVariablesTotal = 0;

    int32 AnimBlueprints      = 0;
    int32 AnimStateMachines   = 0;
    int32 AnimStates          = 0;
    int32 AnimTransitions     = 0;

    FBPeekCoverageStats& operator+=(const FBPeekCoverageStats& O)
    {
        Blueprints          += O.Blueprints;
        TotalGraphNodes     += O.TotalGraphNodes;
        ReachableExecNodes  += O.ReachableExecNodes;
        PureDataNodes       += O.PureDataNodes;
        OrphanExecNodes     += O.OrphanExecNodes;
        CommentNodes        += O.CommentNodes;
        LocalVariablesTotal += O.LocalVariablesTotal;
        AnimBlueprints      += O.AnimBlueprints;
        AnimStateMachines   += O.AnimStateMachines;
        AnimStates          += O.AnimStates;
        AnimTransitions     += O.AnimTransitions;
        return *this;
    }
};

/**
 * Emits the `## Logic` section as pseudo-code. Each event / function
 * entry renders as:
 *
 *   ### Event BeginPlay
 *
 *   ```ue
 *   PushWidgetToLayer(
 *       self        = GetGameUILayout(),
 *       LayerTag    = (TagName="UI.Layer.Game"),
 *       WidgetClass = W_HUDLayout_C,
 *   )
 *   ```
 *
 * Pure-data sources inline as expressions. Multi-consumer pure-data
 * (output referenced from ≥2 pin-consumers across the graph) extracts
 * into temp vars at first use inside each event's fence. Control-flow
 * nodes (Branch / Sequence / Cast / Switch) render with explicit
 * keyword blocks.
 */
class FBPeekGraphWalker
{
public:
    static FBPeekCoverageStats Write(FBPeekMarkdownWriter& W, UBlueprint* BP)
    {
        FBPeekCoverageStats Stats;
#if WITH_EDITOR
        if (!BP) return Stats;
        Stats.Blueprints = 1;

        CurrentCommentBoxes = CollectCommentBoxes(BP);

        // Collect every node across all graphs (dedup via TSet).
        TSet<UEdGraphNode*> AllGraphNodes;
        for (UEdGraph* G : BP->UbergraphPages)
            if (G) for (UEdGraphNode* N : G->Nodes) if (N) AllGraphNodes.Add(N);
        for (UEdGraph* G : BP->FunctionGraphs)
            if (G) for (UEdGraphNode* N : G->Nodes) if (N) AllGraphNodes.Add(N);
        Stats.TotalGraphNodes = AllGraphNodes.Num();

        // Sum local variables across function entries — reported in coverage.
        for (UEdGraph* G : BP->FunctionGraphs)
            if (G) for (UEdGraphNode* N : G->Nodes)
                if (UK2Node_FunctionEntry* FE = Cast<UK2Node_FunctionEntry>(N))
                    Stats.LocalVariablesTotal += FE->LocalVariables.Num();

        // Pre-compute the multi-consumer temp-var map: pure-data nodes whose
        // output feeds ≥2 distinct consumers get extracted into a named temp
        // at first use. Names derived from node title (snake-cased short form).
        TempVarNames = ComputeTempVarNames(AllGraphNodes);

        // Collect entries in stable order (top-to-bottom, left-to-right).
        TArray<UEdGraphNode*> Entries;
        for (UEdGraph* G : BP->UbergraphPages) CollectEntries(G, Entries);
        for (UEdGraph* G : BP->FunctionGraphs) CollectEntries(G, Entries);

        TSet<UEdGraphNode*> AllReached;
        if (Entries.Num() > 0)
        {
            Entries.Sort([](const UEdGraphNode& A, const UEdGraphNode& B){
                if (A.NodePosY != B.NodePosY) return A.NodePosY < B.NodePosY;
                return A.NodePosX < B.NodePosX;
            });

            W.WriteHeading(2, TEXT("Logic"));
            W.WriteLine();

            for (UEdGraphNode* Entry : Entries)
            {
                TSet<UEdGraphNode*> EntryVisited = WriteEntry(W, Entry);
                AllReached.Append(EntryVisited);
            }
        }

        // Classify each node into exactly one coverage bucket.
        TArray<UEdGraphNode*> OrphansToEmit;
        for (UEdGraphNode* N : AllGraphNodes)
        {
            if (Cast<UEdGraphNode_Comment>(N)) { Stats.CommentNodes++; continue; }

            bool bHasExecPin = false;
            for (UEdGraphPin* P : N->Pins)
            {
                if (!P) continue;
                if (P->PinType.PinCategory == TEXT("exec")) { bHasExecPin = true; break; }
            }
            if (!bHasExecPin)          { Stats.PureDataNodes++; continue; }
            if (AllReached.Contains(N)){ Stats.ReachableExecNodes++; continue; }
            Stats.OrphanExecNodes++;
            OrphansToEmit.Add(N);
        }

        if (OrphansToEmit.Num() > 0)
            WriteOrphanSection(W, OrphansToEmit);

        // ## Variable access section for BPs with heavy mutable-state
        // patterns (any variable with >10 total read+write nodes
        // across the graph). Signals global-state-like variables
        // that AI readers should be cautious about.
        WriteVariableAccessSection(W, BP);

        CurrentCommentBoxes.Empty();
        TempVarNames.Empty();
#endif
        return Stats;
    }

#if WITH_EDITOR
private:
    // ---------------------------------------------------------------------
    // Walker-scoped shared state. Commandlet is single-threaded.
    // ---------------------------------------------------------------------
    struct FCommentBox
    {
        FString   Text;
        int32     PosX, PosY, Width, Height;
        UEdGraph* Graph;
    };
    inline static TArray<FCommentBox> CurrentCommentBoxes;

    /** node → stable temp-var name (snake_case). Populated by
     *  ComputeTempVarNames at the start of Write(). */
    inline static TMap<UEdGraphNode*, FString> TempVarNames;

    // ---------------------------------------------------------------------
    // Entry-point collection and ordering.
    // ---------------------------------------------------------------------
    static void CollectEntries(UEdGraph* G, TArray<UEdGraphNode*>& Out)
    {
        if (!G) return;
        for (UEdGraphNode* N : G->Nodes)
            if (N && IsEntryNode(N)) Out.Add(N);
    }

    static bool IsEntryNode(UEdGraphNode* N)
    {
        if (Cast<UK2Node_Event>(N)) return true;
        if (Cast<UK2Node_CustomEvent>(N)) return true;
        if (Cast<UK2Node_FunctionEntry>(N)) return true;

        const FString ClassName = N->GetClass()->GetName();
        if (ClassName.Contains(TEXT("InputAction")) ||
            ClassName.Contains(TEXT("InputAxis"))   ||
            ClassName.Contains(TEXT("InputKey"))    ||
            ClassName.EndsWith(TEXT("Event")))
        {
            bool bOut = false, bIn = false;
            for (UEdGraphPin* P : N->Pins)
            {
                if (P->PinType.PinCategory != TEXT("exec")) continue;
                if (P->Direction == EGPD_Output) bOut = true;
                else                             bIn  = true;
            }
            if (bOut && !bIn) return true;
        }
        return false;
    }

    // ---------------------------------------------------------------------
    // Comment boxes (item #4 from prior sprint).
    // ---------------------------------------------------------------------
    static TArray<FCommentBox> CollectCommentBoxes(UBlueprint* BP)
    {
        TArray<FCommentBox> Result;
        auto Visit = [&](UEdGraph* G)
        {
            if (!G) return;
            for (UEdGraphNode* N : G->Nodes)
            {
                UEdGraphNode_Comment* C = Cast<UEdGraphNode_Comment>(N);
                if (!C) continue;
                FCommentBox B;
                B.Text   = C->NodeComment;
                B.PosX   = C->NodePosX;
                B.PosY   = C->NodePosY;
                B.Width  = C->NodeWidth;
                B.Height = C->NodeHeight;
                B.Graph  = G;
                Result.Add(B);
            }
        };
        for (UEdGraph* G : BP->UbergraphPages) Visit(G);
        for (UEdGraph* G : BP->FunctionGraphs) Visit(G);
        return Result;
    }

    /** Return `// in "Box"` comment for nodes inside a comment box, or
     *  empty if none matches. Pseudo-code code-fence style uses `//`
     *  line comments instead of the previous `_(in "...")_` MD suffix. */
    static FString NodeBoxCommentSuffix(UEdGraphNode* N)
    {
        if (!N || CurrentCommentBoxes.Num() == 0) return FString();
        UEdGraph* NG = N->GetGraph();
        const int32 NX = N->NodePosX;
        const int32 NY = N->NodePosY;

        TArray<FString> Matches;
        for (const FCommentBox& B : CurrentCommentBoxes)
        {
            if (B.Graph != NG) continue;
            if (NX < B.PosX || NX > B.PosX + B.Width) continue;
            if (NY < B.PosY || NY > B.PosY + B.Height) continue;
            if (B.Text.IsEmpty()) continue;
            FString T = B.Text;
            T.ReplaceInline(TEXT("\n"), TEXT(" "));
            T.ReplaceInline(TEXT("\r"), TEXT(" "));
            Matches.Add(T);
        }
        if (Matches.Num() == 0) return FString();

        FString Joined;
        for (int32 i = 0; i < Matches.Num(); ++i)
        {
            if (i > 0) Joined += TEXT(", ");
            Joined += FString::Printf(TEXT("\"%s\""), *Matches[i]);
        }
        return FString::Printf(TEXT("  // in %s"), *Joined);
    }

    // ---------------------------------------------------------------------
    // Temp-var detection & naming.
    // ---------------------------------------------------------------------

    /** True when this pure-data node should NOT be extracted into a temp
     *  var regardless of usage count. VariableGet/VariableSet are trivial
     *  name references and look uglier as temp vars. */
    static bool IsAlwaysInline(UEdGraphNode* N)
    {
        if (!N) return true;
        if (Cast<UK2Node_VariableGet>(N)) return true;
        return false;
    }

    static bool IsPureDataNode(UEdGraphNode* N)
    {
        if (!N) return false;
        for (UEdGraphPin* P : N->Pins)
            if (P && P->PinType.PinCategory == TEXT("exec")) return false;
        return true;
    }

    /** Scan the whole graph set, pick pure-data nodes whose output-pin
     *  link count totals ≥2 (i.e. multiple consumers), and allocate a
     *  stable snake_case name for each. Emission is at first use in the
     *  per-event fence (FlushPendingBindings below). Cast nodes are
     *  ALWAYS extracted (even if single-consumer) because they produce
     *  a named typed value and reading `abmc` reads far better than
     *  `Cast To AudioButtonMatrixColumn.AsAudioButtonMatrixColumn`. */
    static TMap<UEdGraphNode*, FString> ComputeTempVarNames(
        const TSet<UEdGraphNode*>& AllNodes)
    {
        struct FEligible { UEdGraphNode* Node; FString Root; };
        TArray<FEligible> Elig;
        for (UEdGraphNode* N : AllNodes)
        {
            if (IsAlwaysInline(N)) continue;
            // Always-extract: Cast To X nodes → always worth an alias.
            if (Cast<UK2Node_DynamicCast>(N))
            {
                FEligible E;
                E.Node = N;
                E.Root = TempVarRoot(N);
                Elig.Add(MoveTemp(E));
                continue;
            }
            if (!IsPureDataNode(N)) continue;
            int32 OutLinks = 0;
            for (UEdGraphPin* P : N->Pins)
                if (P && P->Direction == EGPD_Output)
                    OutLinks += P->LinkedTo.Num();
            if (OutLinks < 2) continue;
            FEligible E;
            E.Node = N;
            E.Root = TempVarRoot(N);
            Elig.Add(MoveTemp(E));
        }

        // Stable order for deterministic names: sort by (Root, node
        // position) so collisions resolve predictably run-to-run.
        Elig.Sort([](const FEligible& A, const FEligible& B){
            if (A.Root != B.Root) return A.Root < B.Root;
            if (A.Node->NodePosY != B.Node->NodePosY) return A.Node->NodePosY < B.Node->NodePosY;
            return A.Node->NodePosX < B.Node->NodePosX;
        });

        // Resolve collisions: first occurrence keeps the root, later
        // occurrences get `_2`, `_3`, ...
        TMap<FString, int32> RootCount;
        TMap<UEdGraphNode*, FString> Out;
        for (const FEligible& E : Elig)
        {
            int32& N = RootCount.FindOrAdd(E.Root);
            ++N;
            FString Name = (N == 1) ? E.Root : FString::Printf(TEXT("%s_%d"), *E.Root, N);
            Out.Add(E.Node, MoveTemp(Name));
        }
        return Out;
    }

    /** Derive a snake_case short-form name from a node's display title. */
    static FString TempVarRoot(UEdGraphNode* N)
    {
        const FString Title = NodeTitle(N);
        if (Title.IsEmpty()) return TEXT("tmp");

        // Strip common verb prefix — "Get Actor Forward Vector" →
        // "Actor Forward Vector" → last ≤3 word stems → "forward_vec".
        TArray<FString> Words;
        Title.ParseIntoArrayWS(Words);
        if (Words.Num() > 0 && (Words[0].Equals(TEXT("Get"), ESearchCase::IgnoreCase)))
            Words.RemoveAt(0);

        // For Cast nodes: "Cast To PlayerController" → "pc".
        if (Words.Num() >= 3 && Words[0].Equals(TEXT("Cast"), ESearchCase::IgnoreCase)
            && Words[1].Equals(TEXT("To"), ESearchCase::IgnoreCase))
        {
            return AcronymOfClassName(Words.Last());
        }

        // Keep the last up-to-3 words, lower-case, snake-join, strip non-ident chars.
        const int32 Start = FMath::Max(0, Words.Num() - 3);
        TArray<FString> Keep;
        for (int32 i = Start; i < Words.Num(); ++i)
        {
            FString W = Words[i].ToLower();
            FString Cleaned;
            for (TCHAR C : W)
            {
                if (FChar::IsAlnum(C) || C == TEXT('_')) Cleaned.AppendChar(C);
            }
            if (!Cleaned.IsEmpty()) Keep.Add(MoveTemp(Cleaned));
        }
        if (Keep.Num() == 0) return TEXT("tmp");
        // Shorten common long words.
        for (FString& S : Keep)
        {
            if (S == TEXT("vector"))    S = TEXT("vec");
            else if (S == TEXT("rotator")) S = TEXT("rot");
            else if (S == TEXT("transform")) S = TEXT("xform");
            else if (S == TEXT("location")) S = TEXT("loc");
            else if (S == TEXT("rotation")) S = TEXT("rot");
            else if (S == TEXT("character")) S = TEXT("char");
            else if (S == TEXT("controller")) S = TEXT("ctrl");
            else if (S == TEXT("subsystem")) S = TEXT("sub");
        }
        return FString::Join(Keep, TEXT("_"));
    }

    /** "PlayerController" → "pc". Used for Cast target type naming. */
    static FString AcronymOfClassName(const FString& Name)
    {
        FString Out;
        for (int32 i = 0; i < Name.Len(); ++i)
        {
            const TCHAR C = Name[i];
            if (FChar::IsUpper(C))
                Out.AppendChar(FChar::ToLower(C));
        }
        if (Out.IsEmpty())
            Out = Name.Left(FMath::Min(3, Name.Len())).ToLower();
        return Out;
    }

    // ---------------------------------------------------------------------
    // Per-entry emission.
    // ---------------------------------------------------------------------
    struct FEmitState
    {
        TSet<UEdGraphNode*> Visited;             // exec chain cycle guard
        TSet<UEdGraphNode*> EmittedTempVars;     // temp-var bindings already written
        int32 RecursionGuard = 0;                // hard cap on nested Branch/Cast depth

        // Spaghetti-signal counters — tracked through the whole emit
        // for the event, inspected after the fence to decide whether
        // to print a "⚠ complex" footnote.
        int32 ExecNodeCount = 0;
        int32 BranchNodeCount = 0;
    };

    static TSet<UEdGraphNode*> WriteEntry(FBPeekMarkdownWriter& W, UEdGraphNode* Entry)
    {
        W.WriteHeading(3, NodeTitle(Entry));

        if (!Entry->NodeComment.IsEmpty())
        {
            TArray<FString> Lines;
            Entry->NodeComment.ParseIntoArrayLines(Lines);
            for (const FString& L : Lines) W.WriteLine(FString::Printf(TEXT("> %s"), *L));
        }

        WriteFunctionLocals(W, Entry);

        // Quick check: does the entry actually drive an exec chain? If the
        // primary exec output isn't connected we skip the (otherwise empty)
        // code fence entirely — avoids "```ue\n```" for events with no body.
        UEdGraphNode* FirstExec = FollowExecOut(Entry, NAME_None);
        if (!FirstExec)
        {
            W.WriteLine(TEXT("*(empty)*"));
            W.WriteLine();
            TSet<UEdGraphNode*> Visited;
            Visited.Add(Entry);
            return Visited;
        }

        W.WriteLine(TEXT("```ue"));

        FEmitState State;
        EmitChainFromEntry(W, Entry, State, /*Indent*/ 0);

        W.WriteLine(TEXT("```"));

        // Spaghetti signal: >30 exec nodes or >5 branches — flag it.
        constexpr int32 ComplexNodeThreshold   = 30;
        constexpr int32 ComplexBranchThreshold = 5;
        if (State.ExecNodeCount  > ComplexNodeThreshold ||
            State.BranchNodeCount > ComplexBranchThreshold)
        {
            W.WriteLine();
            W.WriteLine(FString::Printf(
                TEXT("_⚠ complex: %d exec node%s, %d branch/sequence node%s — consider splitting._"),
                State.ExecNodeCount,   State.ExecNodeCount   == 1 ? TEXT("") : TEXT("s"),
                State.BranchNodeCount, State.BranchNodeCount == 1 ? TEXT("") : TEXT("s")));
        }

        W.WriteLine();

        State.Visited.Add(Entry);
        return State.Visited;
    }

    /** Emit a `**Locals:**` bullet list (remains outside the code fence
     *  — it's declaration-style metadata, not in-code statements). */
    static void WriteFunctionLocals(FBPeekMarkdownWriter& W, UEdGraphNode* Entry)
    {
        UK2Node_FunctionEntry* FE = Cast<UK2Node_FunctionEntry>(Entry);
        if (!FE || FE->LocalVariables.Num() == 0) return;

        W.WriteLine(TEXT("**Locals:**"));
        for (const FBPVariableDescription& V : FE->LocalVariables)
        {
            FString Line = FString::Printf(TEXT("- `%s` : %s"),
                *V.VarName.ToString(),
                *PinTypeToShort(V.VarType));
            if (!V.DefaultValue.IsEmpty())
                Line += FString::Printf(TEXT(" = `%s`"), *V.DefaultValue);
            if (!V.Category.IsEmpty() && V.Category.ToString() != TEXT("Default"))
                Line += FString::Printf(TEXT(" _(cat: %s)_"), *V.Category.ToString());
            W.WriteLine(Line);
        }
        W.WriteLine();
    }

    // ---------------------------------------------------------------------
    // Exec chain walking (pseudo-code emission).
    // ---------------------------------------------------------------------

    /** Starting from <Entry>, follow the main exec output and emit each
     *  node in sequence. Keyword nodes (Branch / Sequence / Switch / Cast)
     *  render with explicit block syntax and recurse for their sub-chains. */
    static void EmitChainFromEntry(FBPeekMarkdownWriter& W, UEdGraphNode* Entry,
                                   FEmitState& State, int32 Indent)
    {
        UEdGraphNode* Cur = SkipExecKnots(FollowExecOut(Entry, NAME_None), State.Visited);
        EmitChain(W, Cur, State, Indent);
    }

    static void EmitChain(FBPeekMarkdownWriter& W, UEdGraphNode* Start,
                          FEmitState& State, int32 Indent)
    {
        if (State.RecursionGuard > 16) return;
        UEdGraphNode* Cur = Start;
        while (Cur && !State.Visited.Contains(Cur))
        {
            State.Visited.Add(Cur);
            ++State.ExecNodeCount;
            if (Cast<UK2Node_IfThenElse>(Cur)       ||
                Cast<UK2Node_ExecutionSequence>(Cur) ||
                Cast<UK2Node_Switch>(Cur))
                ++State.BranchNodeCount;

            // Flush any pending temp-var bindings that THIS node's inputs
            // would reference. Ensures dependency-before-consumer order.
            FlushPendingBindings(W, Cur, State, Indent);

            const EKeywordResult KW = EmitKeywordNode(W, Cur, State, Indent);
            if (KW == EKeywordResult::Done) return;               // fully walked
            if (KW == EKeywordResult::NotHandled)
                EmitStandardCall(W, Cur, Indent);
            // EKeywordResult::EmittedContinue: keyword wrote its line,
            // we just advance to the next exec node below.

            Cur = SkipExecKnots(FollowExecOut(Cur, NAME_None), State.Visited);
        }
    }

    /** Emit temp-var bindings for any as-yet-unemitted extract-eligible
     *  pure-data sources reachable from <N>'s input pins. Recursively
     *  pre-emits nested dependencies first. */
    static void FlushPendingBindings(FBPeekMarkdownWriter& W, UEdGraphNode* N,
                                     FEmitState& State, int32 Indent)
    {
        for (UEdGraphPin* P : N->Pins)
        {
            if (!P || P->Direction != EGPD_Input) continue;
            if (P->PinType.PinCategory == TEXT("exec")) continue;
            FlushBindingsForPin(W, P, State, Indent);
        }
    }

    static void FlushBindingsForPin(FBPeekMarkdownWriter& W, UEdGraphPin* P,
                                    FEmitState& State, int32 Indent)
    {
        if (!P || P->LinkedTo.Num() == 0) return;
        UEdGraphPin* Src = SkipReroutes(P->LinkedTo[0]);
        if (!Src || !Src->GetOwningNode()) return;
        UEdGraphNode* SrcNode = Src->GetOwningNode();

        // Exec nodes (Cast with exec pins, events, function calls) emit
        // themselves through the exec-chain walker. We don't pre-bind
        // them here, and we don't recurse into their inputs (their own
        // step will handle that when they're visited).
        bool bIsExec = false;
        for (UEdGraphPin* PP : SrcNode->Pins)
            if (PP && PP->PinType.PinCategory == TEXT("exec"))
            { bIsExec = true; break; }
        if (bIsExec) return;

        // Recurse into SrcNode's pure-data inputs first, regardless of
        // whether SrcNode itself is extract-eligible. Deeper extract-
        // eligible nodes still need their bindings emitted in dep order.
        for (UEdGraphPin* IP : SrcNode->Pins)
        {
            if (!IP || IP->Direction != EGPD_Input) continue;
            if (IP->PinType.PinCategory == TEXT("exec")) continue;
            FlushBindingsForPin(W, IP, State, Indent);
        }

        // Now emit a binding for this node if it's extract-eligible.
        FString* TempName = TempVarNames.Find(SrcNode);
        if (!TempName) return;
        if (State.EmittedTempVars.Contains(SrcNode)) return;
        State.EmittedTempVars.Add(SrcNode);

        const FString RHS = RenderNodeAsExpression(SrcNode, /*Depth*/ 0,
            /*NoTempSub*/ true);  // inline expr — don't substitute self
        const FString IndentStr = FString::ChrN(Indent * 4, TEXT(' '));
        W.WriteLine(FString::Printf(TEXT("%s%s = %s"), *IndentStr, **TempName, *RHS));
    }

    /** Generic function/event-call emission: `Name(pin = expr, ...)`.
     *  Arg lines aligned on `=` for scannability. */
    static void EmitStandardCall(FBPeekMarkdownWriter& W, UEdGraphNode* N, int32 Indent)
    {
        const FString Name = NodeTitle(N);
        TArray<TPair<FString, FString>> Args = CollectCallArgs(N);
        const FString Indent1 = FString::ChrN(Indent * 4, TEXT(' '));
        const FString Indent2 = FString::ChrN((Indent + 1) * 4, TEXT(' '));

        FString DisabledComment;
        if (N->GetDesiredEnabledState() == ENodeEnabledState::Disabled)
            DisabledComment = TEXT("  // disabled");
        FString BoxComment = NodeBoxCommentSuffix(N);

        // Designer comment on node: emit as `// comment` lines above.
        if (!N->NodeComment.IsEmpty())
        {
            TArray<FString> Lines;
            N->NodeComment.ParseIntoArrayLines(Lines);
            for (const FString& L : Lines)
                W.WriteLine(FString::Printf(TEXT("%s// %s"), *Indent1, *L));
        }

        if (Args.Num() == 0)
        {
            W.WriteLine(FString::Printf(TEXT("%s%s()%s%s"),
                *Indent1, *Name, *BoxComment, *DisabledComment));
            return;
        }

        int32 KeyWidth = 0;
        for (const auto& KV : Args)
            KeyWidth = FMath::Max(KeyWidth, KV.Key.Len());

        W.WriteLine(FString::Printf(TEXT("%s%s(%s%s"),
            *Indent1, *Name, *BoxComment, *DisabledComment));
        for (int32 i = 0; i < Args.Num(); ++i)
        {
            const FString Pad = FString::ChrN(KeyWidth - Args[i].Key.Len(), TEXT(' '));
            W.WriteLine(FString::Printf(TEXT("%s%s%s = %s,"),
                *Indent2, *Args[i].Key, *Pad, *Args[i].Value));
        }
        W.WriteLine(FString::Printf(TEXT("%s)"), *Indent1));
    }

    /** Collect (name, value-expression) pairs for every input pin that
     *  should appear in the call. Skips exec pins + noisy self-unlinked. */
    static TArray<TPair<FString, FString>> CollectCallArgs(UEdGraphNode* N)
    {
        TArray<TPair<FString, FString>> Out;
        for (UEdGraphPin* P : N->Pins)
        {
            if (!P || P->Direction != EGPD_Input) continue;
            if (P->PinType.PinCategory == TEXT("exec")) continue;
            if (P->PinName == TEXT("self") && P->LinkedTo.Num() == 0) continue;
            TSet<UEdGraphNode*> Cycle;
            FString Val = RenderPinExpression(P, /*Depth*/ 0, Cycle, /*NoTempSub*/ false);
            if (Val.IsEmpty()) continue;
            Out.Add({ P->PinName.ToString(), Val });
        }
        return Out;
    }

    /** Render a whole pure-data node as an inline expression. Used by
     *  the temp-var flush path when we need to emit `tempname = Foo(...)`. */
    static FString RenderNodeAsExpression(UEdGraphNode* N, int32 Depth, bool bNoTempSub)
    {
        if (!N) return FString();
        if (Cast<UK2Node_VariableGet>(N))
            return Cast<UK2Node_VariableGet>(N)->GetVarNameString();

        TSet<UEdGraphNode*> Cycle;
        Cycle.Add(N); // self-cycle prevention
        TArray<FString> Args;
        for (UEdGraphPin* P : N->Pins)
        {
            if (!P || P->Direction != EGPD_Input) continue;
            if (P->PinType.PinCategory == TEXT("exec")) continue;
            if (P->PinName == TEXT("self") && P->LinkedTo.Num() == 0) continue;
            FString V = RenderPinExpression(P, Depth + 1, Cycle, /*NoTempSub*/ false);
            if (V.IsEmpty()) continue;
            Args.Add(FString::Printf(TEXT("%s = %s"), *P->PinName.ToString(), *V));
        }
        if (Args.Num() == 0)
            return FString::Printf(TEXT("%s()"), *NodeTitle(N));
        return FString::Printf(TEXT("%s(%s)"), *NodeTitle(N), *FString::Join(Args, TEXT(", ")));
    }

    /** Render an input pin as a right-hand-side expression:
     *    - `false`, `1.0`, `"text"`   — unlinked literal.
     *    - `MyVar`                    — K2Node_VariableGet.
     *    - `tempvar_name`             — linked to an extract-eligible node.
     *    - `/* result of Spawn Actor *\/` — linked to an exec node.
     *    - `GetForwardVector()`       — linked to a pure-data node, inlined.
     *    - `Add(A = 1, B = 2)`        — recursive inline.
     */
    static FString RenderPinExpression(UEdGraphPin* P, int32 Depth,
                                       TSet<UEdGraphNode*>& Cycle,
                                       bool bNoTempSub)
    {
        if (!P) return FString();

        if (P->LinkedTo.Num() > 0)
        {
            UEdGraphPin* Src = SkipReroutes(P->LinkedTo[0]);
            if (Src && Src->GetOwningNode())
            {
                UEdGraphNode* SrcNode = Src->GetOwningNode();

                if (UK2Node_VariableGet* VG = Cast<UK2Node_VariableGet>(SrcNode))
                    return VG->GetVarNameString();

                // Extract-eligible → substitute temp-var name (unless we ARE
                // that temp var's binding, in which case we render inline).
                if (!bNoTempSub)
                {
                    if (FString* Tn = TempVarNames.Find(SrcNode))
                        return *Tn;
                }

                if (Cycle.Contains(SrcNode))
                    return FString::Printf(TEXT("/* cycle via %s */"), *NodeTitle(SrcNode));

                if (Depth >= MaxInputPinDepth)
                    return FString::Printf(TEXT("%s(...)"), *NodeTitle(SrcNode));

                bool bIsExec = false;
                for (UEdGraphPin* PP : SrcNode->Pins)
                    if (PP && PP->PinType.PinCategory == TEXT("exec"))
                    { bIsExec = true; break; }
                if (bIsExec)
                {
                    // Reference the source node's specific output pin name
                    // when meaningful — "IA_Move.ActionValue" beats
                    // "/* result of IA_Move */" for AI and humans both.
                    const FString PinName = Src->PinName.ToString();
                    if (!PinName.IsEmpty() &&
                        PinName != TEXT("then") && PinName != TEXT("Then") &&
                        PinName != TEXT("Completed"))
                    {
                        return FString::Printf(TEXT("%s.%s"), *NodeTitle(SrcNode), *PinName);
                    }
                    return FString::Printf(TEXT("/* result of %s */"), *NodeTitle(SrcNode));
                }

                Cycle.Add(SrcNode);
                TArray<FString> InnerArgs;
                for (UEdGraphPin* InP : SrcNode->Pins)
                {
                    if (!InP || InP->Direction != EGPD_Input) continue;
                    if (InP->PinType.PinCategory == TEXT("exec")) continue;
                    if (InP->PinName == TEXT("self") && InP->LinkedTo.Num() == 0) continue;
                    FString V = RenderPinExpression(InP, Depth + 1, Cycle, false);
                    if (V.IsEmpty()) continue;
                    InnerArgs.Add(FString::Printf(TEXT("%s = %s"), *InP->PinName.ToString(), *V));
                }
                Cycle.Remove(SrcNode);

                if (InnerArgs.Num() == 0)
                    return FString::Printf(TEXT("%s()"), *NodeTitle(SrcNode));
                return FString::Printf(TEXT("%s(%s)"), *NodeTitle(SrcNode),
                    *FString::Join(InnerArgs, TEXT(", ")));
            }
        }

        // Unlinked literal fallback.
        if (!P->DefaultValue.IsEmpty()) return P->DefaultValue;
        if (P->DefaultObject) return P->DefaultObject->GetName();
        if (!P->DefaultTextValue.IsEmpty())
            return FString::Printf(TEXT("\"%s\""), *P->DefaultTextValue.ToString());
        return FString();
    }

    // ---------------------------------------------------------------------
    // Control-flow keyword nodes: Branch, Sequence, Switch, Cast, Macro.
    // ---------------------------------------------------------------------

    /** Result of attempting to render a node as a keyword-style block.
     *   - NotHandled:       not a keyword node, fall through to generic call.
     *   - Done:             keyword walked all its exec sub-chains; caller
     *                       must stop (no further "main" chain exists).
     *   - EmittedContinue:  keyword emitted its own line, but the outer
     *                       exec chain still needs to advance (e.g. Cast
     *                       continues on "then"/Succeeded pin). */
    enum class EKeywordResult : uint8 { NotHandled, Done, EmittedContinue };

    static EKeywordResult EmitKeywordNode(FBPeekMarkdownWriter& W, UEdGraphNode* N,
                                          FEmitState& State, int32 Indent)
    {
        if (UK2Node_IfThenElse* B = Cast<UK2Node_IfThenElse>(N))
            return EmitBranch(W, B, State, Indent) ? EKeywordResult::Done : EKeywordResult::NotHandled;
        if (Cast<UK2Node_ExecutionSequence>(N))
            return EmitSequence(W, N, State, Indent) ? EKeywordResult::Done : EKeywordResult::NotHandled;
        if (Cast<UK2Node_Switch>(N))
            return EmitSwitch(W, N, State, Indent) ? EKeywordResult::Done : EKeywordResult::NotHandled;
        if (UK2Node_DynamicCast* C = Cast<UK2Node_DynamicCast>(N))
        {
            EmitCast(W, C, State, Indent);
            return EKeywordResult::EmittedContinue;
        }
        if (UK2Node_MacroInstance* M = Cast<UK2Node_MacroInstance>(N))
        {
            EmitMacro(W, M, State, Indent);
            return EKeywordResult::EmittedContinue;
        }
        return EKeywordResult::NotHandled;
    }

    static bool EmitBranch(FBPeekMarkdownWriter& W, UK2Node_IfThenElse* B,
                           FEmitState& State, int32 Indent)
    {
        const FString I1 = FString::ChrN(Indent * 4, TEXT(' '));
        const FString I2 = FString::ChrN((Indent + 1) * 4, TEXT(' '));

        // Resolve Condition input.
        FString CondExpr;
        for (UEdGraphPin* P : B->Pins)
        {
            if (!P || P->Direction != EGPD_Input) continue;
            if (P->PinName == TEXT("Condition"))
            {
                TSet<UEdGraphNode*> Cycle;
                CondExpr = RenderPinExpression(P, 0, Cycle, false);
                break;
            }
        }
        if (CondExpr.IsEmpty()) CondExpr = TEXT("/* unresolved */");

        W.WriteLine(FString::Printf(TEXT("%sBranch"), *I1));
        W.WriteLine(FString::Printf(TEXT("%sCondition = %s"), *I2, *CondExpr));

        const bool bHasThen = FollowExecOut(B, TEXT("then")) != nullptr;
        const bool bHasElse = FollowExecOut(B, TEXT("else")) != nullptr;

        if (bHasThen)
        {
            W.WriteLine(FString::Printf(TEXT("%sThen:"), *I2));
            State.RecursionGuard++;
            UEdGraphNode* Next = SkipExecKnots(FollowExecOut(B, TEXT("then")), State.Visited);
            EmitChain(W, Next, State, Indent + 2);
            State.RecursionGuard--;
        }
        if (bHasElse)
        {
            W.WriteLine(FString::Printf(TEXT("%sElse:"), *I2));
            State.RecursionGuard++;
            UEdGraphNode* Next = SkipExecKnots(FollowExecOut(B, TEXT("else")), State.Visited);
            EmitChain(W, Next, State, Indent + 2);
            State.RecursionGuard--;
        }
        return true;
    }

    static bool EmitSequence(FBPeekMarkdownWriter& W, UEdGraphNode* N,
                             FEmitState& State, int32 Indent)
    {
        const FString I1 = FString::ChrN(Indent * 4, TEXT(' '));
        const FString I2 = FString::ChrN((Indent + 1) * 4, TEXT(' '));
        W.WriteLine(FString::Printf(TEXT("%sSequence:"), *I1));

        int32 Idx = 0;
        for (UEdGraphPin* P : N->Pins)
        {
            if (!P || P->Direction != EGPD_Output) continue;
            if (P->PinType.PinCategory != TEXT("exec")) continue;
            if (P->LinkedTo.Num() == 0) { ++Idx; continue; }
            W.WriteLine(FString::Printf(TEXT("%sThen %d:"), *I2, Idx));
            State.RecursionGuard++;
            UEdGraphNode* Next = SkipExecKnots(FollowExecOut(N, P->PinName), State.Visited);
            EmitChain(W, Next, State, Indent + 2);
            State.RecursionGuard--;
            ++Idx;
        }
        return true;
    }

    static bool EmitSwitch(FBPeekMarkdownWriter& W, UEdGraphNode* N,
                           FEmitState& State, int32 Indent)
    {
        const FString I1 = FString::ChrN(Indent * 4, TEXT(' '));
        const FString I2 = FString::ChrN((Indent + 1) * 4, TEXT(' '));

        // Find selector pin (non-exec input).
        FString SelExpr = TEXT("/* selector */");
        for (UEdGraphPin* P : N->Pins)
        {
            if (!P || P->Direction != EGPD_Input) continue;
            if (P->PinType.PinCategory == TEXT("exec")) continue;
            if (P->PinName == TEXT("self") && P->LinkedTo.Num() == 0) continue;
            TSet<UEdGraphNode*> Cycle;
            FString V = RenderPinExpression(P, 0, Cycle, false);
            if (!V.IsEmpty()) { SelExpr = V; break; }
        }

        W.WriteLine(FString::Printf(TEXT("%sSwitch on %s"), *I1, *SelExpr));

        for (UEdGraphPin* P : N->Pins)
        {
            if (!P || P->Direction != EGPD_Output) continue;
            if (P->PinType.PinCategory != TEXT("exec")) continue;
            if (P->LinkedTo.Num() == 0) continue;
            const FString PinName = P->PinName.ToString();
            const FString Label = PinName.Equals(TEXT("Default"))
                ? FString(TEXT("Default:"))
                : FString::Printf(TEXT("Case %s:"), *PinName);
            W.WriteLine(FString::Printf(TEXT("%s%s"), *I2, *Label));
            State.RecursionGuard++;
            UEdGraphNode* Next = SkipExecKnots(FollowExecOut(N, P->PinName), State.Visited);
            EmitChain(W, Next, State, Indent + 2);
            State.RecursionGuard--;
        }
        return true;
    }

    static void EmitCast(FBPeekMarkdownWriter& W, UK2Node_DynamicCast* C,
                         FEmitState& State, int32 Indent)
    {
        const FString I1 = FString::ChrN(Indent * 4, TEXT(' '));
        const FString I2 = FString::ChrN((Indent + 1) * 4, TEXT(' '));

        // Target class — suffix of node title ("Cast To PlayerController").
        const FString Title = NodeTitle(C);

        // Object input pin value.
        FString ObjExpr = TEXT("/* object */");
        for (UEdGraphPin* P : C->Pins)
        {
            if (!P || P->Direction != EGPD_Input) continue;
            if (P->PinType.PinCategory == TEXT("exec")) continue;
            const FString PinName = P->PinName.ToString();
            if (PinName == TEXT("self")) continue;
            if (PinName.Contains(TEXT("Object")) || P->LinkedTo.Num() > 0)
            {
                TSet<UEdGraphNode*> Cycle;
                ObjExpr = RenderPinExpression(P, 0, Cycle, false);
                break;
            }
        }

        // Cast itself may be extracted to a temp var (extract-eligible check).
        FString* TempName = TempVarNames.Find(C);
        const FString VarName = TempName ? **TempName : AcronymOfClassName(ExtractCastTarget(Title));
        W.WriteLine(FString::Printf(TEXT("%s%s = %s(Object = %s)"),
            *I1, *VarName, *Title, *ObjExpr));

        // Walk Succeeded then (fall-through) — it continues as main exec.
        // Walk CastFailed as a nested block if connected.
        UEdGraphNode* FailNext = SkipExecKnots(FollowExecOut(C, TEXT("CastFailed")), State.Visited);
        if (FailNext)
        {
            W.WriteLine(FString::Printf(TEXT("%sCast Failed:"), *I2));
            State.RecursionGuard++;
            EmitChain(W, FailNext, State, Indent + 2);
            State.RecursionGuard--;
        }
    }

    static FString ExtractCastTarget(const FString& Title)
    {
        // "Cast To PlayerController" → "PlayerController". "Cast to X" too.
        TArray<FString> Words;
        Title.ParseIntoArrayWS(Words);
        if (Words.Num() >= 3 && Words[0].Equals(TEXT("Cast"), ESearchCase::IgnoreCase) &&
            Words[1].Equals(TEXT("To"), ESearchCase::IgnoreCase))
        {
            return Words.Last();
        }
        return TEXT("Target");
    }

    /** Macro nodes (ForEachLoop, WhileLoop, DoOnce, FlipFlop, …): multiple
     *  exec outputs with macro-specific names. Emit as a keyword block with
     *  each non-default pin labelled, continuation via `then` / `Completed`
     *  handled by the caller falling through. */
    static void EmitMacro(FBPeekMarkdownWriter& W, UK2Node_MacroInstance* M,
                          FEmitState& State, int32 Indent)
    {
        const FString I1 = FString::ChrN(Indent * 4, TEXT(' '));
        const FString I2 = FString::ChrN((Indent + 1) * 4, TEXT(' '));
        const FString Title = NodeTitle(M);

        // Args: input pins minus exec.
        TArray<TPair<FString, FString>> Args = CollectCallArgs(M);

        // Header line: "ForEachLoop(Array = ...)" or "DoOnce" if no args.
        if (Args.Num() == 0)
            W.WriteLine(FString::Printf(TEXT("%s%s"), *I1, *Title));
        else
        {
            TArray<FString> Flat;
            for (const auto& KV : Args)
                Flat.Add(FString::Printf(TEXT("%s = %s"), *KV.Key, *KV.Value));
            W.WriteLine(FString::Printf(TEXT("%s%s(%s)"), *I1, *Title,
                *FString::Join(Flat, TEXT(", "))));
        }

        // Non-primary exec outputs (LoopBody, Completed-but-not-through-chain).
        for (UEdGraphPin* P : M->Pins)
        {
            if (!P || P->Direction != EGPD_Output) continue;
            if (P->PinType.PinCategory != TEXT("exec")) continue;
            if (P->LinkedTo.Num() == 0) continue;
            const FString PinName = P->PinName.ToString();
            if (PinName == TEXT("then") || PinName == TEXT("Then")) continue;
            W.WriteLine(FString::Printf(TEXT("%s%s:"), *I2, *PinName));
            State.RecursionGuard++;
            UEdGraphNode* Next = SkipExecKnots(FollowExecOut(M, P->PinName), State.Visited);
            EmitChain(W, Next, State, Indent + 2);
            State.RecursionGuard--;
        }

        // Fall-through: outer chain continues on "then"/"Then"/"Completed".
    }

    // ---------------------------------------------------------------------
    // Exec-chain navigation primitives.
    // ---------------------------------------------------------------------

    static UEdGraphNode* SkipExecKnots(UEdGraphNode* N, TSet<UEdGraphNode*>& Visited)
    {
        while (N && N->GetClass()->GetName() == TEXT("K2Node_Knot"))
        {
            Visited.Add(N);
            UEdGraphNode* Next = nullptr;
            for (UEdGraphPin* P : N->Pins)
            {
                if (!P || P->Direction != EGPD_Output) continue;
                if (P->PinType.PinCategory != TEXT("exec")) continue;
                if (P->LinkedTo.Num() == 0) return nullptr;
                UEdGraphPin* Target = P->LinkedTo[0];
                if (Target && Target->GetOwningNode()) { Next = Target->GetOwningNode(); break; }
            }
            if (!Next || Next == N) return nullptr;
            N = Next;
        }
        return N;
    }

    static UEdGraphNode* FollowExecOut(UEdGraphNode* N, const FName& SpecificPin)
    {
        if (!N) return nullptr;
        if (SpecificPin != NAME_None)
        {
            for (UEdGraphPin* P : N->Pins)
            {
                if (!P || P->Direction != EGPD_Output) continue;
                if (P->PinType.PinCategory != TEXT("exec")) continue;
                if (P->PinName != SpecificPin) continue;
                if (P->LinkedTo.Num() == 0) continue;
                UEdGraphPin* Target = P->LinkedTo[0];
                if (Target && Target->GetOwningNode()) return Target->GetOwningNode();
            }
            return nullptr;
        }
        static const FName PreferredNames[] = {
            FName(TEXT("then")), FName(TEXT("Then")), FName(TEXT("Completed"))
        };
        for (const FName& Preferred : PreferredNames)
        {
            for (UEdGraphPin* P : N->Pins)
            {
                if (!P || P->Direction != EGPD_Output) continue;
                if (P->PinType.PinCategory != TEXT("exec")) continue;
                if (P->PinName != Preferred) continue;
                if (P->LinkedTo.Num() == 0) continue;
                UEdGraphPin* Target = P->LinkedTo[0];
                if (Target && Target->GetOwningNode()) return Target->GetOwningNode();
            }
        }
        for (UEdGraphPin* P : N->Pins)
        {
            if (!P || P->Direction != EGPD_Output) continue;
            if (P->PinType.PinCategory != TEXT("exec")) continue;
            if (P->LinkedTo.Num() == 0) continue;
            UEdGraphPin* Target = P->LinkedTo[0];
            if (Target && Target->GetOwningNode()) return Target->GetOwningNode();
        }
        return nullptr;
    }

    static UEdGraphPin* SkipReroutes(UEdGraphPin* P)
    {
        while (P && P->GetOwningNode())
        {
            UEdGraphNode* N = P->GetOwningNode();
            if (N->GetClass()->GetName() != TEXT("K2Node_Knot")) return P;
            UEdGraphPin* Next = nullptr;
            for (UEdGraphPin* KP : N->Pins)
            {
                if (!KP || KP->Direction != EGPD_Input) continue;
                if (KP->LinkedTo.Num() > 0) { Next = KP->LinkedTo[0]; break; }
            }
            if (!Next || Next == P) return P;
            P = Next;
        }
        return P;
    }

    // ---------------------------------------------------------------------
    // Variable access summary — spaghetti-state detector.
    // ---------------------------------------------------------------------

    /** Scan all graphs for UK2Node_VariableGet / UK2Node_VariableSet.
     *  Emit "## Variable access" only for vars whose total references
     *  (reads + writes) exceed the threshold (>10 nodes). Section is a
     *  spaghetti-detection signal — these are the de-facto shared
     *  globals that deserve extra attention from reviewers. */
    static void WriteVariableAccessSection(FBPeekMarkdownWriter& W, UBlueprint* BP)
    {
        if (!BP) return;
        constexpr int32 Threshold = 10;

        struct FAccess
        {
            int32 ReadCount  = 0;
            int32 WriteCount = 0;
            TSet<FString> ReadEntries;
            TSet<FString> WriteEntries;
        };
        TMap<FString, FAccess> Access;

        auto VisitGraph = [&](UEdGraph* G)
        {
            if (!G) return;
            // Collect entry titles within this graph.
            TArray<FString> EntryTitles;
            for (UEdGraphNode* N : G->Nodes)
                if (N && IsEntryNode(N)) EntryTitles.Add(NodeTitle(N));
            // Fallback to graph name if this graph has no entries
            // (rare — macro graphs etc.).
            if (EntryTitles.Num() == 0) EntryTitles.Add(G->GetName());

            for (UEdGraphNode* N : G->Nodes)
            {
                if (!N) continue;
                FString VarName;
                bool    bWrite = false;
                if (UK2Node_VariableGet* VG = Cast<UK2Node_VariableGet>(N))
                {
                    VarName = VG->GetVarNameString();
                    bWrite  = false;
                }
                else if (UK2Node_VariableSet* VS = Cast<UK2Node_VariableSet>(N))
                {
                    VarName = VS->GetVarNameString();
                    bWrite  = true;
                }
                else continue;
                if (VarName.IsEmpty()) continue;

                FAccess& A = Access.FindOrAdd(VarName);
                if (bWrite) { ++A.WriteCount; for (const FString& E : EntryTitles) A.WriteEntries.Add(E); }
                else        { ++A.ReadCount;  for (const FString& E : EntryTitles) A.ReadEntries.Add(E); }
            }
        };
        for (UEdGraph* G : BP->UbergraphPages) VisitGraph(G);
        for (UEdGraph* G : BP->FunctionGraphs) VisitGraph(G);

        // Filter to variables above threshold.
        TArray<FString> Loud;
        for (const auto& KV : Access)
            if (KV.Value.ReadCount + KV.Value.WriteCount > Threshold)
                Loud.Add(KV.Key);
        if (Loud.Num() == 0) return;
        Loud.Sort();

        W.WriteHeading(2, TEXT("Variable access"));
        W.WriteLine();
        W.WriteLine(TEXT("_Variables with heavy read/write traffic — shared mutable state worth reviewing._"));
        W.WriteLine();

        for (const FString& Name : Loud)
        {
            const FAccess& A = Access[Name];
            W.WriteLine(FString::Printf(TEXT("### `%s`"), *Name));
            W.WriteLine(FString::Printf(
                TEXT("%d read%s · %d write%s"),
                A.ReadCount,  A.ReadCount  == 1 ? TEXT("") : TEXT("s"),
                A.WriteCount, A.WriteCount == 1 ? TEXT("") : TEXT("s")));
            if (A.ReadEntries.Num() > 0)
            {
                TArray<FString> R = A.ReadEntries.Array();
                R.Sort();
                W.WriteLine(FString::Printf(TEXT("- **Read** in: %s"),
                    *FString::Join(R, TEXT(", "))));
            }
            if (A.WriteEntries.Num() > 0)
            {
                TArray<FString> Wr = A.WriteEntries.Array();
                Wr.Sort();
                W.WriteLine(FString::Printf(TEXT("- **Written** in: %s"),
                    *FString::Join(Wr, TEXT(", "))));
            }
            W.WriteLine();
        }
    }

    // ---------------------------------------------------------------------
    // Orphan-node section.
    // ---------------------------------------------------------------------
    static void WriteOrphanSection(FBPeekMarkdownWriter& W,
                                   const TArray<UEdGraphNode*>& Orphans)
    {
        W.WriteHeading(2, FString::Printf(TEXT("Orphan nodes (%d)"), Orphans.Num()));
        W.WriteLine();
        W.WriteLine(TEXT("_Exec nodes that no entry reaches — WIP, dead code, or nodes pending wiring._"));
        W.WriteLine();

        TMap<UEdGraph*, TArray<UEdGraphNode*>> ByGraph;
        TArray<UEdGraph*> GraphOrder;
        for (UEdGraphNode* N : Orphans)
        {
            UEdGraph* G = N->GetGraph();
            if (!ByGraph.Contains(G)) GraphOrder.Add(G);
            ByGraph.FindOrAdd(G).Add(N);
        }

        for (UEdGraph* G : GraphOrder)
        {
            const FString GraphName = G ? G->GetName() : TEXT("(unknown graph)");
            W.WriteHeading(3, GraphName);

            for (UEdGraphNode* N : ByGraph[G])
            {
                const FString Title = NodeTitle(N);
                const FString DisabledTag = N->GetDesiredEnabledState() == ENodeEnabledState::Disabled
                    ? FString(TEXT(" _(disabled)_")) : FString();
                const FString BoxSuffix = NodeBoxMdSuffix(N);
                W.WriteLine(FString::Printf(TEXT("- **%s**%s%s"),
                    *Title, *DisabledTag, *BoxSuffix));

                if (!N->NodeComment.IsEmpty())
                    W.WriteLine(FString::Printf(TEXT("    > %s"),
                        *N->NodeComment.Replace(TEXT("\n"), TEXT(" "))));

                for (UEdGraphPin* P : N->Pins)
                {
                    if (!P || P->Direction != EGPD_Input) continue;
                    if (P->PinType.PinCategory == TEXT("exec")) continue;
                    if (P->PinName == TEXT("self") && P->LinkedTo.Num() == 0) continue;
                    TSet<UEdGraphNode*> Cycle;
                    const FString Val = RenderPinExpression(P, 0, Cycle, false);
                    if (Val.IsEmpty()) continue;
                    W.WriteLine(FString::Printf(TEXT("    - %s: %s"),
                        *P->PinName.ToString(), *Val));
                }
            }
            W.WriteLine();
        }
    }

    /** Orphan-section uses MD-style " _(in \"X\")_" suffix since it's
     *  rendered as a bullet list, not inside a code fence. */
    static FString NodeBoxMdSuffix(UEdGraphNode* N)
    {
        if (!N || CurrentCommentBoxes.Num() == 0) return FString();
        UEdGraph* NG = N->GetGraph();
        const int32 NX = N->NodePosX;
        const int32 NY = N->NodePosY;

        TArray<FString> Matches;
        for (const FCommentBox& B : CurrentCommentBoxes)
        {
            if (B.Graph != NG) continue;
            if (NX < B.PosX || NX > B.PosX + B.Width) continue;
            if (NY < B.PosY || NY > B.PosY + B.Height) continue;
            if (B.Text.IsEmpty()) continue;
            FString T = B.Text;
            T.ReplaceInline(TEXT("\n"), TEXT(" "));
            T.ReplaceInline(TEXT("\r"), TEXT(" "));
            Matches.Add(T);
        }
        if (Matches.Num() == 0) return FString();

        FString Joined;
        for (int32 i = 0; i < Matches.Num(); ++i)
        {
            if (i > 0) Joined += TEXT(", ");
            Joined += FString::Printf(TEXT("\"%s\""), *Matches[i]);
        }
        return FString::Printf(TEXT(" _(in %s)_"), *Joined);
    }

    // ---------------------------------------------------------------------
    // Misc helpers.
    // ---------------------------------------------------------------------
    static FString NodeTitle(UEdGraphNode* N)
    {
        if (!N) return FString();
        FString T = N->GetNodeTitle(ENodeTitleType::MenuTitle).ToString();
        T.ReplaceInline(TEXT("\n"), TEXT(" "));
        T.ReplaceInline(TEXT("\r"), TEXT(" "));
        return T;
    }

    static FString PinTypeToShort(const FEdGraphPinType& T)
    {
        FString Base = T.PinCategory.ToString();
        if (T.PinSubCategoryObject.IsValid()) Base = T.PinSubCategoryObject->GetName();

        if (T.ContainerType == EPinContainerType::Array)
            return FString::Printf(TEXT("%s[]"), *Base);
        if (T.ContainerType == EPinContainerType::Set)
            return FString::Printf(TEXT("TSet<%s>"), *Base);
        if (T.ContainerType == EPinContainerType::Map)
        {
            FString ValBase = T.PinValueType.TerminalCategory.ToString();
            if (T.PinValueType.TerminalSubCategoryObject.IsValid())
                ValBase = T.PinValueType.TerminalSubCategoryObject->GetName();
            return FString::Printf(TEXT("TMap<%s, %s>"), *Base, *ValBase);
        }
        return Base;
    }

    static constexpr int32 MaxInputPinDepth = 3;
#endif // WITH_EDITOR
};
