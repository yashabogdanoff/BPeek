#pragma once

#include "CoreMinimal.h"
#include "BPeekMarkdownWriter.h"
#include "BPeekTextUnwrap.h"
#include "BPeekAssetLinks.h"
#include "BPeekAssetPathHelpers.h"
#include "BPeekUsedBy.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BlackboardAssetProvider.h"

class FBPeekBehaviorTreeWriter
{
public:
    //
    // UBehaviorTree renderer. Walks from RootNode recursively, emits
    // a hierarchical markdown listing with per-node Decorators/Services
    // and properties. Full-markdown layout (no mermaid) — easier for
    // AI agents to embed and search via RAG than a graphviz-ish
    // pseudo-graph.
    //
    static void WriteBehaviorTree(FBPeekMarkdownWriter& W, UBehaviorTree* BT,
                                  const TMap<FString, TArray<FString>>& Refs,
                                  const TSet<FString>& Known,
                                  bool bVerboseMode)
    {
        if (!BT) return;
        const FString AssetPath = BT->GetPathName();
        const FString DisplayPath = bVerboseMode ? AssetPath : FBPeekAssetPath::Compact(AssetPath);

        W.WriteHeading(1, FString::Printf(TEXT("%s (Behavior Tree)"), *BT->GetName()));
        W.WriteLine();
        W.WriteMetaRowCode(TEXT("Asset path"), DisplayPath);

        // Blackboard reference — soft link; only write if set.
        if (UBlackboardData* BB = BT->BlackboardAsset)
        {
            const FString BBPath = BB->GetPathName();
            const FString BBDisplay = bVerboseMode ? BBPath : FBPeekAssetPath::Compact(BBPath);
            W.WriteMetaRowCode(TEXT("Blackboard"), BBDisplay);
        }

        // Count nodes up-front so the Tree heading has a total.
        const int32 NodeTotal = CountNodes(BT->RootNode);
        W.WriteMetaRow(TEXT("Nodes"), FString::FromInt(NodeTotal));
        W.WriteLine();

        // Root decorators — exist on UBehaviorTree outside RootNode, used
        // as global entry conditions. Render before the tree so they
        // appear as "gate" context.
        if (BT->RootDecorators.Num() > 0)
        {
            W.WriteHeading(2, FString::Printf(TEXT("Root decorators (%d)"), BT->RootDecorators.Num()));
            W.WriteLine();
            for (UBTDecorator* Dec : BT->RootDecorators)
                EmitDecoratorLine(W, Dec, AssetPath, Known);
            W.WriteLine();
        }

        if (!BT->RootNode)
        {
            W.WriteLine(TEXT("_Behavior Tree has no RootNode set — tree empty or broken._"));
            FBPeekUsedBy::Write(W, Refs, AssetPath, Known);
            return;
        }

        W.WriteHeading(2, TEXT("Tree"));
        W.WriteLine();
        EmitNodeRecursive(W, BT->RootNode, /*Depth=*/0, AssetPath, Known, bVerboseMode);
        W.WriteLine();

        FBPeekUsedBy::Write(W, Refs, AssetPath, Known);
    }

    //
    // UBlackboardData renderer. Lists every FBlackboardEntry in this asset
    // (plus parent-inherited keys when Parent is set) with key type and
    // optional comment. Schema-level description — does not walk values.
    //
    static void WriteBlackboardData(FBPeekMarkdownWriter& W, UBlackboardData* BB,
                                    const TMap<FString, TArray<FString>>& Refs,
                                    const TSet<FString>& Known,
                                    bool bVerboseMode)
    {
        if (!BB) return;
        const FString AssetPath = BB->GetPathName();
        const FString DisplayPath = bVerboseMode ? AssetPath : FBPeekAssetPath::Compact(AssetPath);

        W.WriteHeading(1, FString::Printf(TEXT("%s (Blackboard Data)"), *BB->GetName()));
        W.WriteLine();
        W.WriteMetaRowCode(TEXT("Asset path"), DisplayPath);
        if (UBlackboardData* Parent = BB->Parent)
        {
            const FString ParentPath = Parent->GetPathName();
            const FString ParentDisplay = bVerboseMode ? ParentPath : FBPeekAssetPath::Compact(ParentPath);
            W.WriteMetaRowCode(TEXT("Parent"), ParentDisplay);
        }
        W.WriteMetaRow(TEXT("Keys"), FString::FromInt(BB->Keys.Num()));
        W.WriteLine();

        if (BB->Keys.Num() == 0)
        {
            W.WriteLine(TEXT("_No keys defined (possibly inherits everything from Parent)._"));
            FBPeekUsedBy::Write(W, Refs, AssetPath, Known);
            return;
        }

        if (bVerboseMode)
        {
            W.WriteHeading(2, FString::Printf(TEXT("Keys (%d)"), BB->Keys.Num()));
            W.WriteLine();
            W.WriteLine(TEXT("| Name | Type | Category | Description |"));
            W.WriteLine(TEXT("|---|---|---|---|"));
            for (const FBlackboardEntry& E : BB->Keys)
            {
                const FString TypeName = E.KeyType ? E.KeyType->GetClass()->GetName() : FString(TEXT("?"));
                const FString Cat = E.EntryCategory.ToString();
                const FString Desc = E.EntryDescription.TrimStartAndEnd();
                W.WriteLine(FString::Printf(TEXT("| `%s` | `%s` | %s | %s |"),
                    *E.EntryName.ToString(),
                    *TypeName,
                    Cat.IsEmpty() ? TEXT("—") : *Cat,
                    Desc.IsEmpty() ? TEXT("—") : *Desc));
            }
            W.WriteLine();
        }
        else
        {
            // Compact: one line per key, joined.
            TArray<FString> Parts;
            Parts.Reserve(BB->Keys.Num());
            for (const FBlackboardEntry& E : BB->Keys)
            {
                const FString TypeName = E.KeyType ? StripKeyTypePrefix(E.KeyType->GetClass()->GetName()) : FString(TEXT("?"));
                Parts.Add(FString::Printf(TEXT("`%s`:`%s`"), *E.EntryName.ToString(), *TypeName));
            }
            W.WriteLine(FString::Printf(TEXT("**Keys (%d):** %s"),
                BB->Keys.Num(), *FString::Join(Parts, TEXT(", "))));
            W.WriteLine();
        }

        FBPeekUsedBy::Write(W, Refs, AssetPath, Known);
    }

    //
    // BP subclass renderer: BP whose ParentClass descends from
    // UBTTaskNode / UBTDecorator / UBTService. Renders CDO properties
    // (the BP's configurable defaults) and exec bytecode is already
    // covered by core's Blueprint writer — we only add the BT flavour
    // label. Heavy Blueprint rendering stays in core.
    //
    static void WriteBTNodeBlueprint(FBPeekMarkdownWriter& W, UBlueprint* BP, const TCHAR* Flavour,
                                     const TMap<FString, TArray<FString>>& Refs,
                                     const TSet<FString>& Known,
                                     bool bVerboseMode)
    {
        if (!BP) return;
        const FString AssetPath = BP->GetPathName();
        const FString DisplayPath = bVerboseMode ? AssetPath : FBPeekAssetPath::Compact(AssetPath);

        W.WriteHeading(1, FString::Printf(TEXT("%s (%s)"), *BP->GetName(), Flavour));
        W.WriteLine();
        W.WriteMetaRowCode(TEXT("Asset path"), DisplayPath);
        if (BP->ParentClass)
            W.WriteMetaRowCode(TEXT("Parent"), BP->ParentClass->GetName());

        UObject* Subject = nullptr;
        UClass* SubjectClass = nullptr;
        if (BP->GeneratedClass)
        {
            SubjectClass = BP->GeneratedClass;
            Subject = BP->GeneratedClass->GetDefaultObject();
        }
        W.WriteLine();

        if (!Subject)
        {
            W.WriteLine(TEXT("_BT-node BP CDO unavailable — Blueprint probably failed to compile._"));
            FBPeekUsedBy::Write(W, Refs, AssetPath, Known);
            return;
        }

        // Walk only properties declared on classes between Subject and the
        // closest BT-base (TaskNode/Decorator/Service). Deeper base classes
        // (UBTNode, UObject) have scaffolding we don't want to surface.
        EmitNodePropertiesBlock(W, Subject, SubjectClass, AssetPath, Known, bVerboseMode);

        FBPeekUsedBy::Write(W, Refs, AssetPath, Known);
    }

private:
    static int32 CountNodes(UBTCompositeNode* Root)
    {
        if (!Root) return 0;
        int32 Count = 1;
        for (const FBTCompositeChild& Child : Root->Children)
        {
            if (Child.ChildComposite)
                Count += CountNodes(Child.ChildComposite);
            else if (Child.ChildTask)
                Count += 1;
        }
        return Count;
    }

    /** Recursive emit: one H3 per node, decorators/services listed inline,
     *  then recurse into children (composites) with an increased indent. */
    static void EmitNodeRecursive(FBPeekMarkdownWriter& W, UBTCompositeNode* Composite, int32 Depth,
                                  const FString& AssetPath, const TSet<FString>& Known, bool bVerboseMode)
    {
        if (!Composite) return;

        const FString Indent = FString::ChrN(Depth * 2, TEXT(' '));
        const FString CompositeClassName = StripBTPrefix(Composite->GetClass()->GetName());
        const FString NodeName = Composite->NodeName.IsEmpty()
            ? CompositeClassName
            : FString::Printf(TEXT("%s [%s]"), *Composite->NodeName, *CompositeClassName);

        W.WriteLine(FString::Printf(TEXT("%s- **%s**"), *Indent, *NodeName));

        // Composite-level services (tick every N).
        for (UBTService* Svc : Composite->Services)
            EmitAttachedLine(W, Svc, TEXT("service"), Indent + TEXT("  "), AssetPath, Known);

        // Walk children. Each FBTCompositeChild has an optional composite
        // OR task; decorators attach at the child slot level.
        for (const FBTCompositeChild& Child : Composite->Children)
        {
            // Decorators on this child slot.
            for (UBTDecorator* Dec : Child.Decorators)
                EmitAttachedLine(W, Dec, TEXT("decorator"), Indent + TEXT("  "), AssetPath, Known);

            if (Child.ChildComposite)
            {
                EmitNodeRecursive(W, Child.ChildComposite, Depth + 1, AssetPath, Known, bVerboseMode);
            }
            else if (Child.ChildTask)
            {
                EmitTaskLine(W, Child.ChildTask, Depth + 1, AssetPath, Known);
            }
        }
    }

    static void EmitTaskLine(FBPeekMarkdownWriter& W, UBTTaskNode* Task, int32 Depth,
                             const FString& AssetPath, const TSet<FString>& Known)
    {
        if (!Task) return;
        const FString Indent = FString::ChrN(Depth * 2, TEXT(' '));
        const FString TaskClassName = StripBTPrefix(Task->GetClass()->GetName());
        const FString NodeName = Task->NodeName.IsEmpty()
            ? TaskClassName
            : FString::Printf(TEXT("%s [%s]"), *Task->NodeName, *TaskClassName);

        // Inline interesting properties of the task CDO — skip scaffolding.
        const FString PropSummary = CollectPropertiesCompact(Task, UBTTaskNode::StaticClass(), AssetPath, Known);
        if (PropSummary.IsEmpty())
            W.WriteLine(FString::Printf(TEXT("%s- **%s**"), *Indent, *NodeName));
        else
            W.WriteLine(FString::Printf(TEXT("%s- **%s** — %s"), *Indent, *NodeName, *PropSummary));

        // Services can also live on tasks (UBTTaskNode::Services isn't a
        // thing in current UE — services attach to composites only). Skip.
    }

    static void EmitDecoratorLine(FBPeekMarkdownWriter& W, UBTDecorator* Dec,
                                  const FString& AssetPath, const TSet<FString>& Known)
    {
        if (!Dec) return;
        const FString ClassName = StripBTPrefix(Dec->GetClass()->GetName());
        const FString Title = Dec->NodeName.IsEmpty()
            ? ClassName
            : FString::Printf(TEXT("%s [%s]"), *Dec->NodeName, *ClassName);
        const FString PropSummary = CollectPropertiesCompact(Dec, UBTDecorator::StaticClass(), AssetPath, Known);
        if (PropSummary.IsEmpty())
            W.WriteLine(FString::Printf(TEXT("- _decorator:_ **%s**"), *Title));
        else
            W.WriteLine(FString::Printf(TEXT("- _decorator:_ **%s** — %s"), *Title, *PropSummary));
    }

    static void EmitAttachedLine(FBPeekMarkdownWriter& W, UBTNode* Node, const TCHAR* Label,
                                 const FString& Indent, const FString& AssetPath, const TSet<FString>& Known)
    {
        if (!Node) return;
        const FString ClassName = StripBTPrefix(Node->GetClass()->GetName());
        const FString Title = Node->NodeName.IsEmpty()
            ? ClassName
            : FString::Printf(TEXT("%s [%s]"), *Node->NodeName, *ClassName);

        UClass* FilterBase = nullptr;
        if (Node->IsA<UBTDecorator>()) FilterBase = UBTDecorator::StaticClass();
        else if (Node->IsA<UBTService>()) FilterBase = UBTService::StaticClass();
        else FilterBase = UBTNode::StaticClass();

        const FString PropSummary = CollectPropertiesCompact(Node, FilterBase, AssetPath, Known);
        if (PropSummary.IsEmpty())
            W.WriteLine(FString::Printf(TEXT("%s- _%s:_ **%s**"), *Indent, Label, *Title));
        else
            W.WriteLine(FString::Printf(TEXT("%s- _%s:_ **%s** — %s"), *Indent, Label, *Title, *PropSummary));
    }

    /** Iterate reflection properties on `Subject` restricted to fields
     *  declared on subclasses of `FilterBase` (skipping UBTNode/UObject
     *  scaffolding). Return a compact one-line summary or empty string. */
    static FString CollectPropertiesCompact(UObject* Subject, UClass* FilterBase,
                                            const FString& AssetPath, const TSet<FString>& Known)
    {
        if (!Subject || !FilterBase) return FString();
        TArray<FString> Parts;
        for (TFieldIterator<FProperty> It(Subject->GetClass()); It; ++It)
        {
            FProperty* P = *It;
            if (!P->HasAnyPropertyFlags(CPF_Edit)) continue;
            if (P->HasAnyPropertyFlags(CPF_Transient)) continue;
            UClass* Owner = P->GetOwnerClass();
            // Filter out base-scaffolding properties (owner at or above FilterBase).
            if (!Owner) continue;
            if (Owner == UObject::StaticClass()) continue;
            if (Owner == UBTNode::StaticClass()) continue;
            if (Owner == FilterBase) continue;

            FString Val = ExportPropertyValue(P, Subject);
            if (Val.IsEmpty()) continue;
            Val = FBPeekTextUnwrap::Unwrap(Val);
            Val = FBPeekAssetLinks::Linkify(Val, AssetPath, Known);
            Val.ReplaceInline(TEXT(","), TEXT(";"));
            Parts.Add(FString::Printf(TEXT("`%s`=%s"), *P->GetName(), *Val));
        }
        return FString::Join(Parts, TEXT(", "));
    }

    static void EmitNodePropertiesBlock(FBPeekMarkdownWriter& W, UObject* Subject, UClass* SubjectClass,
                                        const FString& AssetPath, const TSet<FString>& Known, bool bVerboseMode)
    {
        if (!Subject || !SubjectClass) return;
        struct FProp { FString Name; FString Value; };
        TArray<FProp> Props;
        for (TFieldIterator<FProperty> It(SubjectClass); It; ++It)
        {
            FProperty* P = *It;
            if (!P->HasAnyPropertyFlags(CPF_Edit)) continue;
            if (P->HasAnyPropertyFlags(CPF_Transient)) continue;
            UClass* Owner = P->GetOwnerClass();
            if (Owner == UObject::StaticClass()) continue;
            if (Owner == UBTNode::StaticClass()) continue;

            FString Val = ExportPropertyValue(P, Subject);
            if (Val.IsEmpty()) continue;
            Props.Add({ P->GetName(), Val });
        }
        if (Props.Num() == 0)
        {
            W.WriteLine(TEXT("_No editable properties._"));
            return;
        }
        Props.Sort([](const FProp& A, const FProp& B){
            return A.Name.Compare(B.Name, ESearchCase::CaseSensitive) < 0;
        });

        if (bVerboseMode)
        {
            W.WriteHeading(2, TEXT("Properties"));
            W.WriteLine();
            for (const FProp& P : Props)
            {
                FString V = FBPeekTextUnwrap::Unwrap(P.Value);
                V = FBPeekAssetLinks::Linkify(V, AssetPath, Known);
                W.WriteLine(FString::Printf(TEXT("- **%s**: %s"), *P.Name, *V));
            }
        }
        else
        {
            TArray<FString> Parts;
            Parts.Reserve(Props.Num());
            for (const FProp& P : Props)
            {
                FString V = FBPeekTextUnwrap::Unwrap(P.Value);
                V = FBPeekAssetLinks::Linkify(V, AssetPath, Known);
                V.ReplaceInline(TEXT(","), TEXT(";"));
                Parts.Add(FString::Printf(TEXT("`%s`=%s"), *P.Name, *V));
            }
            W.WriteLine(FString::Printf(TEXT("**Properties (%d):** %s"),
                Parts.Num(), *FString::Join(Parts, TEXT(", "))));
            W.WriteLine();
        }
    }

    static FString ExportPropertyValue(FProperty* P, UObject* Subject)
    {
        FString Val;
        if (FObjectProperty* OP = CastField<FObjectProperty>(P))
        {
            UObject* V = OP->GetObjectPropertyValue_InContainer(Subject);
            if (V) Val = V->GetPathName();
        }
        else if (FSoftObjectProperty* SP = CastField<FSoftObjectProperty>(P))
        {
            const FSoftObjectPtr V = SP->GetPropertyValue_InContainer(Subject);
            if (!V.IsNull()) Val = V.ToString();
        }
        else if (FTextProperty* TP = CastField<FTextProperty>(P))
        {
            const FText V = TP->GetPropertyValue_InContainer(Subject);
            if (!V.IsEmpty()) Val = V.ToString();
        }
        else
        {
            P->ExportTextItem_Direct(Val, P->ContainerPtrToValuePtr<void>(Subject), nullptr, nullptr, PPF_None);
            if (Val == TEXT("None") || Val == TEXT("\"\"") || Val == TEXT("()"))
                Val = FString();
        }
        return Val;
    }

    /** Strip UE's BT class prefix so readers see "Selector" not
     *  "BTComposite_Selector", "MoveTo" not "BTTask_MoveTo". */
    static FString StripBTPrefix(const FString& ClassName)
    {
        static const TCHAR* Prefixes[] = {
            TEXT("BTComposite_"), TEXT("BTTask_"), TEXT("BTDecorator_"),
            TEXT("BTService_"), TEXT("BT_"),
        };
        for (const TCHAR* Pref : Prefixes)
        {
            if (ClassName.StartsWith(Pref))
                return ClassName.RightChop(FCString::Strlen(Pref));
        }
        return ClassName;
    }

    /** Strip UBlackboardKeyType_ prefix so "BlackboardKeyType_Object"
     *  becomes "Object". */
    static FString StripKeyTypePrefix(const FString& ClassName)
    {
        static const TCHAR* Pref = TEXT("BlackboardKeyType_");
        if (ClassName.StartsWith(Pref))
            return ClassName.RightChop(FCString::Strlen(Pref));
        return ClassName;
    }
};
