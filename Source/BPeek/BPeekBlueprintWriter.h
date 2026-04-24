#pragma once
#include "CoreMinimal.h"
#include "BPeekMarkdownWriter.h"
#include "BPeekTextUnwrap.h"
#include "BPeekAssetLinks.h"
#include "BPeekAssetPathHelpers.h"
#include "BPeekUsedBy.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Animation/WidgetAnimation.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "EdGraph/EdGraph.h"
#include "BPeekGraphWalker.h"
#include "BPeekAnimStateMachineWriter.h"
#include "BPeekImportTable.h"
#include "BPeekCppSource.h"
#include "BPeekIssuesWriter.h"

class FBPeekBlueprintWriter
{
public:
    /** Returns per-BP coverage stats so the commandlet can aggregate
     *  a project-wide report. */
    /** LogicOverride:
     *    nullptr — `## Logic` (and AnimBP state-machines, when
     *    applicable) go into the main `W` just like before.
     *    non-null — they go into `*LogicOverride` instead. The caller
     *    owns that writer and is expected to `SaveTo()` it at a
     *    companion path (e.g. `<path>/<name>.logic.md`). The main W
     *    gets a one-line "See also: [link]" hint so readers and AI
     *    agents know the companion file exists.
     *
     *  Used by the Blueprint extension in the default (AI-optimised)
     *  mode to keep the main MD small while the Logic section (which
     *  we deliberately never compress — it's pseudo-code people
     *  actually read) lives in a separate file the agent can pull
     *  lazily. Verbose mode keeps everything in a single file. */
    static FBPeekCoverageStats Write(FBPeekMarkdownWriter& W, UBlueprint* BP,
                      const TMap<FString, TArray<FString>>& Refs = TMap<FString, TArray<FString>>(),
                      const TSet<FString>& KnownNormalized = TSet<FString>(),
                      bool bVerboseMode = false,
                      FBPeekMarkdownWriter* LogicOverride = nullptr)
    {
        FBPeekCoverageStats Stats;
        if (!BP) return Stats;
        const FString AssetPath = BP->GetPathName();
        const FString Name = BP->GetName();

        // Header layout:
        //   - Title line carries (Blueprint) / (Widget Blueprint) / (Anim Blueprint).
        //   - Parent + Class merged onto one meta-row; Path on next.
        //   - Implements rendered as a single comma-joined line (if any).
        //   - At-a-glance strip under the header.
        //   - No ## Events section — it duplicates Logic H3s, and
        //     outline plugins of GitHub/VSCode already render a TOC.
        const FString TypeLabel = GetTypeLabel(BP);
        W.WriteLine(FString::Printf(TEXT("# %s  *(%s)*"), *Name, *TypeLabel));
        W.WriteLine();
        WriteHeaderMetaBlock(W, BP, AssetPath, bVerboseMode);
        WriteAtAGlanceStrip(W, BP, Refs, AssetPath);
        W.WriteLine();

        // ## Issues section — Status + EditorValidator signals, plus
        // compiler output when -recompile flag is set. Emitted only if
        // the asset has any issue; clean BPs stay clean.
        FBPeekIssuesWriter::Write(W, BP);

        WriteComponents(W, BP, AssetPath, KnownNormalized, bVerboseMode);
        WriteVariables(W, BP, AssetPath, KnownNormalized, bVerboseMode);
        WriteWidgetTree(W, BP, AssetPath, KnownNormalized);
        WriteWidgetAnimations(W, BP);

        // ## Logic section — the B-UE pseudo-code block. Stays full
        // either way (we never compress code). When LogicOverride is
        // supplied (default AI-optimised mode), the walker writes into
        // the companion file and the main W only gets a "see also"
        // line. In verbose mode LogicOverride is null and the block
        // goes into the main W alongside everything else.
        FBPeekMarkdownWriter& LogicW = LogicOverride ? *LogicOverride : W;
        Stats = FBPeekGraphWalker::Write(LogicW, BP);
        {
            FBPeekAnimStats A = FBPeekAnimStateMachineWriter::Write(LogicW, BP);
            Stats.AnimBlueprints    += A.AnimBlueprints;
            Stats.AnimStateMachines += A.StateMachines;
            Stats.AnimStates        += A.States;
            Stats.AnimTransitions   += A.Transitions;
        }
        if (LogicOverride)
        {
            W.WriteLine(FString::Printf(
                TEXT("**Logic:** see [%s.logic.md](%s.logic.md) — split off "
                     "for AI-friendly loading. Agents pull on demand."),
                *Name, *Name));
            W.WriteLine();
        }
        // Import-table sections via FLinkerLoad::ImportMap: K2 node
        // types, Functions called, Structs used, Enums used, Package
        // dependencies (linked), Other imports (grouped by class).
        FBPeekImportTable::Write(W, BP, KnownNormalized, bVerboseMode);

        FBPeekUsedBy::Write(W, Refs, AssetPath, KnownNormalized);
        return Stats;
    }

private:
    /** "(Blueprint)" / "(Widget Blueprint)" / "(Anim Blueprint)" for the
     *  title suffix — picked via dynamic_cast chain since UAnimBlueprint /
     *  UWidgetBlueprint both inherit UBlueprint. */
    static FString GetTypeLabel(UBlueprint* BP)
    {
        if (Cast<UWidgetBlueprint>(BP)) return TEXT("Widget Blueprint");
        if (BP && BP->GetClass() && BP->GetClass()->GetName() == TEXT("AnimBlueprint"))
            return TEXT("Anim Blueprint");
        // UAnimBlueprint requires the Animation module header; doing a
        // name-string check avoids adding the include just for this label.
        return TEXT("Blueprint");
    }

    /** 2-line meta block under the H1:
     *    **Parent:** `...` → [Source]  ·  **Class:** `...`
     *    **Path:** `...`
     *    **Implements:** `I1` → [Source], `I2` → [Source]     (optional)
     */
    static void WriteHeaderMetaBlock(FBPeekMarkdownWriter& W, UBlueprint* BP,
                                     const FString& AssetPath, bool bVerboseMode = false)
    {
        TArray<FString> Parts;
        if (BP->ParentClass)
        {
            const FString PName = BP->ParentClass->GetName();
            const FString Link = FBPeekCppSource::ResolveClassLink(BP->ParentClass, AssetPath);
            Parts.Add(Link.IsEmpty()
                ? FString::Printf(TEXT("**Parent:** `%s`"), *PName)
                : FString::Printf(TEXT("**Parent:** `%s` → %s"), *PName, *Link));
        }
        if (BP->GeneratedClass)
            Parts.Add(FString::Printf(TEXT("**Class:** `%s`"), *BP->GeneratedClass->GetName()));
        if (Parts.Num() > 0)
            W.WriteLine(FString::Join(Parts, TEXT("  ·  ")));

        const FString DisplayPath = bVerboseMode
            ? FBPeekAssetPath::Normalize(AssetPath)
            : FBPeekAssetPath::Compact(AssetPath);
        W.WriteLine(FString::Printf(TEXT("**Path:** `%s`"), *DisplayPath));

        // Implements — compact single line if any interfaces present.
        if (BP->ImplementedInterfaces.Num() > 0)
        {
            TArray<FString> Items;
            for (const FBPInterfaceDescription& IfDesc : BP->ImplementedInterfaces)
            {
                UClass* IfCls = IfDesc.Interface;
                if (!IfCls) continue;
                const FString IfName = IfCls->GetName();
                const FString Link = FBPeekCppSource::ResolveClassLink(IfCls, AssetPath);
                Items.Add(Link.IsEmpty()
                    ? FString::Printf(TEXT("`%s`"), *IfName)
                    : FString::Printf(TEXT("`%s` → %s"), *IfName, *Link));
            }
            if (Items.Num() > 0)
                W.WriteLine(FString::Printf(TEXT("**Implements:** %s"),
                    *FString::Join(Items, TEXT(", "))));
        }
        W.WriteLine();
    }

    /** At-a-glance strip (italic one-liner) summarising sizes. Dashes a
     *  placeholder when a given count is zero — the line stays stable in
     *  width between BPs, helps eyes skim across files. */
    static void WriteAtAGlanceStrip(FBPeekMarkdownWriter& W, UBlueprint* BP,
                                    const TMap<FString, TArray<FString>>& Refs,
                                    const FString& AssetPath)
    {
        const int32 EventCount     = CountEvents(BP);
        const int32 FunctionCount  = CountFunctions(BP);
        const int32 ImportsCount   = CountImportClass(BP, TEXT("Function"));
        const int32 StructsCount   = CountImportClass(BP, TEXT("ScriptStruct"));
        const int32 DepsCount      = CountImportClass(BP, TEXT("Package"));
        const int32 RefCount       = CountRefs(Refs, AssetPath);

        TArray<FString> Bits;
        Bits.Add(FString::Printf(TEXT("%d event%s"), EventCount, EventCount == 1 ? TEXT("") : TEXT("s")));
        Bits.Add(FString::Printf(TEXT("%d function%s"), FunctionCount, FunctionCount == 1 ? TEXT("") : TEXT("s")));
        Bits.Add(FString::Printf(TEXT("%d fn call%s"), ImportsCount, ImportsCount == 1 ? TEXT("") : TEXT("s")));
        Bits.Add(FString::Printf(TEXT("%d struct%s"), StructsCount, StructsCount == 1 ? TEXT("") : TEXT("s")));
        Bits.Add(FString::Printf(TEXT("%d dep%s"), DepsCount, DepsCount == 1 ? TEXT("") : TEXT("s")));
        Bits.Add(FString::Printf(TEXT("referenced by %d"), RefCount));
        W.WriteLine(FString::Printf(TEXT("*%s*"), *FString::Join(Bits, TEXT(" · "))));
    }

    /** Count entry-points: K2Node_Event / K2Node_CustomEvent / input-style
     *  nodes. Mirrors the retired WriteEvents classifier. */
    static int32 CountEvents(UBlueprint* BP)
    {
        if (!BP) return 0;
        int32 N = 0;
        for (UEdGraph* G : BP->UbergraphPages)
        {
            if (!G) continue;
            for (UEdGraphNode* Node : G->Nodes)
            {
                if (!Node) continue;
                if (Cast<UK2Node_CustomEvent>(Node)) { ++N; continue; }
                if (Cast<UK2Node_Event>(Node))       { ++N; continue; }
                const FString Cls = Node->GetClass()->GetName();
                if (Cls.Contains(TEXT("Input")) ||
                    Cls.EndsWith(TEXT("Event"))   ||
                    Cls.Contains(TEXT("BoundEvent")))
                    ++N;
            }
        }
        return N;
    }

    /** User-defined BP functions (not Event Graph pages). */
    static int32 CountFunctions(UBlueprint* BP)
    {
        if (!BP) return 0;
        int32 N = 0;
        for (UEdGraph* G : BP->FunctionGraphs)
            if (G) ++N;
        return N;
    }

    /** Count imports whose ClassName matches the given tag (Function,
     *  ScriptStruct, Package, …). Reads the linker import table. */
    static int32 CountImportClass(UBlueprint* BP, const TCHAR* Tag)
    {
#if WITH_EDITOR
        if (!BP) return 0;
        UPackage* Pkg = BP->GetOutermost();
        if (!Pkg) return 0;
        FLinkerLoad* Linker = FLinkerLoad::FindExistingLinkerForPackage(Pkg);
        if (!Linker) return 0;
        const FName Key(Tag);
        int32 N = 0;
        TSet<FString> Seen;
        for (const FObjectImport& Imp : Linker->ImportMap)
        {
            if (Imp.ClassName != Key) continue;
            const FString Name = Imp.ObjectName.ToString();
            bool bAlready = false;
            Seen.Add(Name, &bAlready);
            if (!bAlready) ++N;
        }
        return N;
#else
        return 0;
#endif
    }

    static int32 CountRefs(const TMap<FString, TArray<FString>>& Refs,
                           const FString& AssetPath)
    {
        const FString Norm = FBPeekAssetPath::Normalize(AssetPath);
        const TArray<FString>* Entries = Refs.Find(Norm);
        if (!Entries) return 0;
        // De-dup and drop self (mirrors FBPeekUsedBy::Write filtering).
        TSet<FString> Kept;
        for (const FString& R : *Entries)
        {
            const FString N = FBPeekAssetPath::Normalize(R);
            if (N.Equals(Norm)) continue;
            Kept.Add(N);
        }
        return Kept.Num();
    }

    static void WriteComponents(FBPeekMarkdownWriter& W, UBlueprint* BP,
                                const FString& AssetPath, const TSet<FString>& Known,
                                bool bVerboseMode = false)
    {
        if (!BP->SimpleConstructionScript) return;
        struct FComp { FString Name; FString Class; FString AttachTo; };
        TArray<FComp> Comps;
        for (USCS_Node* N : BP->SimpleConstructionScript->GetAllNodes())
        {
            if (!N) continue;
            FComp C;
            C.Name = N->GetVariableName().ToString();
            if (N->ComponentClass) C.Class = N->ComponentClass->GetName();
            if (N->ParentComponentOrVariableName != NAME_None)
                C.AttachTo = N->ParentComponentOrVariableName.ToString();
            Comps.Add(MoveTemp(C));
        }
        if (Comps.Num() == 0) return;

        if (bVerboseMode)
        {
            W.WriteHeading(2, FString::Printf(TEXT("Components (%d)"), Comps.Num()));
            W.WriteLine();
            W.WriteLine(TEXT("| Name | Class | Attach to |"));
            W.WriteLine(TEXT("|---|---|---|"));
            for (const FComp& C : Comps)
            {
                FString ClsCell = C.Class.IsEmpty()
                    ? FString(TEXT("`?`"))
                    : FBPeekAssetLinks::Linkify(FString::Printf(TEXT("`%s`"), *C.Class), AssetPath, Known);
                W.WriteLine(FString::Printf(TEXT("| `%s` | %s | %s |"), *C.Name, *ClsCell, *C.AttachTo));
            }
            W.WriteLine();
            return;
        }

        // Default (AI-optimised) — one-line summary.
        TArray<FString> Parts;
        Parts.Reserve(Comps.Num());
        for (const FComp& C : Comps)
        {
            FString ClsStr = C.Class.IsEmpty() ? FString(TEXT("?")) : C.Class;
            FString Entry = FString::Printf(TEXT("`%s`:`%s`"), *C.Name, *ClsStr);
            if (!C.AttachTo.IsEmpty())
                Entry += FString::Printf(TEXT("→`%s`"), *C.AttachTo);
            Parts.Add(MoveTemp(Entry));
        }
        W.WriteLine(FString::Printf(TEXT("**Components (%d):** %s"),
            Comps.Num(), *FString::Join(Parts, TEXT(", "))));
        W.WriteLine();
    }

    static void WriteVariables(FBPeekMarkdownWriter& W, UBlueprint* BP,
                               const FString& AssetPath, const TSet<FString>& Known,
                               bool bVerboseMode = false)
    {
        if (BP->NewVariables.Num() == 0) return;
        struct FVar { FString Name; FString Type; FString Category; FString Default;
                      FString Tooltip; bool Editable; bool ReadOnly; };
        TArray<FVar> Vars;
        for (const FBPVariableDescription& V : BP->NewVariables)
        {
            FVar X;
            X.Name = V.VarName.ToString();
            X.Type = PinTypeToStr(V.VarType);
            X.Category = V.Category.IsEmpty() ? FString() : V.Category.ToString();
            X.Default = V.DefaultValue;
            X.Editable = (V.PropertyFlags & CPF_Edit) != 0;
            X.ReadOnly = (V.PropertyFlags & CPF_BlueprintReadOnly) != 0;
            for (const FBPVariableMetaDataEntry& MD : V.MetaDataArray)
                if (MD.DataKey == FName(TEXT("tooltip")) && !MD.DataValue.IsEmpty())
                { X.Tooltip = MD.DataValue; break; }
            Vars.Add(MoveTemp(X));
        }

        bool bHasTooltips = false;
        for (const FVar& V : Vars) if (!V.Tooltip.IsEmpty()) { bHasTooltips = true; break; }

        if (bVerboseMode)
        {
            W.WriteHeading(2, FString::Printf(TEXT("Variables (%d)"), Vars.Num()));
            W.WriteLine();
            if (bHasTooltips)
            {
                W.WriteLine(TEXT("| Name | Type | Category | Default | Flags | Tooltip |"));
                W.WriteLine(TEXT("|---|---|---|---|---|---|"));
            }
            else
            {
                W.WriteLine(TEXT("| Name | Type | Category | Default | Flags |"));
                W.WriteLine(TEXT("|---|---|---|---|---|"));
            }
            for (const FVar& V : Vars)
            {
                TArray<FString> Flags;
                if (V.Editable) Flags.Add(TEXT("editable"));
                if (V.ReadOnly) Flags.Add(TEXT("readonly"));
                const FString FlagStr = FString::Join(Flags, TEXT(", "));
                FString Def = FBPeekTextUnwrap::Unwrap(V.Default);
                Def = FBPeekAssetLinks::Linkify(Def, AssetPath, Known);
                if (bHasTooltips)
                {
                    FString Tip = V.Tooltip;
                    Tip.ReplaceInline(TEXT("\n"), TEXT(" "));
                    Tip.ReplaceInline(TEXT("|"), TEXT("/"));
                    W.WriteLine(FString::Printf(
                        TEXT("| `%s` | `%s` | %s | %s | %s | %s |"),
                        *V.Name, *V.Type, *V.Category, *Def, *FlagStr, *Tip));
                }
                else
                {
                    W.WriteLine(FString::Printf(
                        TEXT("| `%s` | `%s` | %s | %s | %s |"),
                        *V.Name, *V.Type, *V.Category, *Def, *FlagStr));
                }
            }
            W.WriteLine();
            return;
        }

        // Default (AI-optimised) — one-line summary.
        TArray<FString> Parts;
        Parts.Reserve(Vars.Num());
        for (const FVar& V : Vars)
        {
            FString Def = FBPeekTextUnwrap::Unwrap(V.Default);
            Def = FBPeekAssetLinks::Linkify(Def, AssetPath, Known);
            TArray<FString> Flags;
            if (V.Editable) Flags.Add(TEXT("ed"));
            if (V.ReadOnly) Flags.Add(TEXT("ro"));

            FString Entry = FString::Printf(TEXT("`%s`:`%s`"), *V.Name, *V.Type);
            if (!Def.IsEmpty())      Entry += FString::Printf(TEXT("=%s"), *Def);
            if (Flags.Num() > 0)     Entry += FString::Printf(TEXT("(%s)"), *FString::Join(Flags, TEXT(",")));
            if (!V.Category.IsEmpty()) Entry += FString::Printf(TEXT("[%s]"), *V.Category);
            if (!V.Tooltip.IsEmpty())
            {
                FString Tip = V.Tooltip;
                Tip.ReplaceInline(TEXT("\n"), TEXT(" "));
                Tip.ReplaceInline(TEXT(","),  TEXT(";"));
                Entry += FString::Printf(TEXT(" \"%s\""), *Tip);
            }
            Parts.Add(MoveTemp(Entry));
        }
        W.WriteLine(FString::Printf(TEXT("**Variables (%d):** %s"),
            Vars.Num(), *FString::Join(Parts, TEXT(", "))));
        W.WriteLine();
    }

    static void WriteWidgetTree(FBPeekMarkdownWriter& W, UBlueprint* BP,
                                const FString& AssetPath, const TSet<FString>& Known)
    {
        UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(BP);
        if (!WBP || !WBP->WidgetTree || !WBP->WidgetTree->RootWidget) return;
        W.WriteHeading(2, TEXT("Widget Tree"));
        W.WriteLine();
        WriteWidgetNode(W, WBP->WidgetTree->RootWidget, 0, AssetPath, Known);
        W.WriteLine();
    }

    static void WriteWidgetNode(FBPeekMarkdownWriter& W, UWidget* N, int32 Depth,
                                const FString& AssetPath, const TSet<FString>& Known)
    {
        if (!N) return;
        const FString Indent = FString::ChrN(Depth * 2, TEXT(' '));
        const FString Cls = FString::Printf(TEXT(" (%s)"), *N->GetClass()->GetName());
        W.WriteLine(FString::Printf(TEXT("%s- **%s**%s"), *Indent, *N->GetName(), *Cls));

        // Editable UPROPERTY snapshot (matches DumpWidget filter).
        TArray<TPair<FString, FString>> Props;
        for (TFieldIterator<FProperty> PIt(N->GetClass()); PIt; ++PIt)
        {
            FProperty* P = *PIt;
            if (!P->HasAnyPropertyFlags(CPF_Edit)) continue;
            if (P->HasAnyPropertyFlags(CPF_Transient)) continue;
            UClass* Owner = P->GetOwnerClass();
            if (Owner == UWidget::StaticClass()) continue;
            if (Owner == UPanelWidget::StaticClass()) continue;
            const FString PropName = P->GetName();
            if (PropName.StartsWith(TEXT("bOverride_")))
            {
                if (FBoolProperty* BP = CastField<FBoolProperty>(P);
                    BP && !BP->GetPropertyValue_InContainer(N)) continue;
            }
            FString Val;
            if (FObjectProperty* OP = CastField<FObjectProperty>(P))
            {
                UObject* V = OP->GetObjectPropertyValue_InContainer(N);
                if (V) Val = V->GetPathName();
            }
            else if (FSoftObjectProperty* SP = CastField<FSoftObjectProperty>(P))
            {
                const FSoftObjectPtr V = SP->GetPropertyValue_InContainer(N);
                if (!V.IsNull()) Val = V.ToString();
            }
            else if (FTextProperty* TP = CastField<FTextProperty>(P))
            {
                const FText V = TP->GetPropertyValue_InContainer(N);
                if (!V.IsEmpty()) Val = V.ToString();
            }
            else
            {
                P->ExportTextItem_Direct(Val, P->ContainerPtrToValuePtr<void>(N), nullptr, nullptr, PPF_None);
                if (Val == TEXT("None") || Val == TEXT("\"\"") || Val == TEXT("()")) Val = FString();
            }
            if (!Val.IsEmpty()) Props.Add({ PropName, Val });
        }
        Props.Sort([](const TPair<FString,FString>& A, const TPair<FString,FString>& B){
            return A.Key.Compare(B.Key, ESearchCase::CaseSensitive) < 0;
        });
        for (const auto& KV : Props)
        {
            FString V = FBPeekTextUnwrap::Unwrap(KV.Value);
            V = FBPeekAssetLinks::Linkify(V, AssetPath, Known);
            W.WriteLine(FString::Printf(TEXT("%s  - %s: %s"), *Indent, *KV.Key, *V));
        }

        if (UPanelWidget* PW = Cast<UPanelWidget>(N))
            for (int32 i = 0; i < PW->GetChildrenCount(); ++i)
                WriteWidgetNode(W, PW->GetChildAt(i), Depth + 1, AssetPath, Known);
    }

    static void WriteWidgetAnimations(FBPeekMarkdownWriter& W, UBlueprint* BP)
    {
        UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(BP);
        if (!WBP || WBP->Animations.Num() == 0) return;
        W.WriteHeading(2, FString::Printf(TEXT("Widget animations (%d)"), WBP->Animations.Num()));
        W.WriteLine();
        for (UWidgetAnimation* A : WBP->Animations)
            if (A) W.WriteLine(FString::Printf(TEXT("- %s"), *A->GetName()));
        W.WriteLine();
    }

    /**
     * Stringify an FEdGraphPinType the same way the commandlet's JSON dump
     * does (PinTypeToStr helper). Extracted so the writer doesn't depend on
     * the main commandlet's local function.
     */
    static FString PinTypeToStr(const FEdGraphPinType& T)
    {
        FString Base = T.PinCategory.ToString();
        if (T.PinSubCategoryObject.IsValid()) Base = T.PinSubCategoryObject->GetName();
        if (T.ContainerType == EPinContainerType::Array)
            return FString::Printf(TEXT("TArray<%s>"), *Base);
        if (T.ContainerType == EPinContainerType::Set)
            return FString::Printf(TEXT("TSet<%s>"), *Base);
        if (T.ContainerType == EPinContainerType::Map)
        {
            FString Val = T.PinValueType.TerminalCategory.ToString();
            if (T.PinValueType.TerminalSubCategoryObject.IsValid())
                Val = T.PinValueType.TerminalSubCategoryObject->GetName();
            return FString::Printf(TEXT("TMap<%s, %s>"), *Base, *Val);
        }
        return Base;
    }
};