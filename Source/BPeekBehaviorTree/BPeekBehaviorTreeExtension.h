#pragma once

#include "CoreMinimal.h"
#include "BPeekExtensionAPI.h"
#include "BPeekBehaviorTreeWriter.h"
#include "BPeekIndexBuilder.h"

#include "Engine/Blueprint.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"

//
// IBPeekExtension adapter for Behavior Tree assets. Handles:
//
//   1. UBehaviorTree    — the tree container (root composite + blackboard
//                         ref). Rendered via FBPeekBehaviorTreeWriter
//                         with full hierarchy + per-node properties.
//
//   2. UBlackboardData  — the blackboard schema asset. Renders the key
//                         table (type, category, description).
//
//   3. BP subclasses of UBTTaskNode / UBTDecorator / UBTService — custom
//                         BP-based BT nodes. Rendered with a BT-flavour
//                         heading and CDO property list (leaving the
//                         heavy exec-graph rendering to core's Blueprint
//                         writer).
//
// Priority 200 beats core's generic Blueprint extension (100), so BP-
// based BT nodes land here.
//
class FBPeekBehaviorTreeExtension : public IBPeekExtension
{
public:
    FName   GetId()          const override { return TEXT("bpeek.behaviortree"); }
    FString GetVersionName() const override { return TEXT("1.0.0"); }
    int32   GetPriority()    const override { return 200; }

    TArray<UClass*> GetHandledClasses() const override
    {
        // BP-subclass routing works via CanHandle priority; core iterates
        // every UBlueprint via TObjectIterator and asks FindFor — no need
        // to list UBlueprint here.
        return { UBehaviorTree::StaticClass(), UBlackboardData::StaticClass() };
    }

    bool CanHandle(UObject* Asset) const override
    {
        if (!Asset) return false;
        if (Asset->HasAnyFlags(RF_ClassDefaultObject)) return false;
        if (Cast<UBehaviorTree>(Asset)) return true;
        if (Cast<UBlackboardData>(Asset)) return true;

        // BT-node BP: walk parent chain for TaskNode/Decorator/Service.
        if (UBlueprint* BP = Cast<UBlueprint>(Asset))
        {
            for (UClass* P = BP->ParentClass; P; P = P->GetSuperClass())
            {
                if (P == UBTTaskNode::StaticClass())  return true;
                if (P == UBTDecorator::StaticClass()) return true;
                if (P == UBTService::StaticClass())   return true;
            }
        }
        return false;
    }

    void Write(FBPeekMarkdownWriter& W, UObject* Asset, const FBPeekScanContext& Ctx) override
    {
        static const TMap<FString, TArray<FString>> EmptyRefs;
        static const TSet<FString> EmptyKnown;
        const auto& Refs  = Ctx.Refs  ? *Ctx.Refs  : EmptyRefs;
        const auto& Known = Ctx.Known ? *Ctx.Known : EmptyKnown;

        if (UBehaviorTree* BT = Cast<UBehaviorTree>(Asset))
        {
            FBPeekBehaviorTreeWriter::WriteBehaviorTree(W, BT, Refs, Known, Ctx.bVerboseMode);
            return;
        }
        if (UBlackboardData* BB = Cast<UBlackboardData>(Asset))
        {
            FBPeekBehaviorTreeWriter::WriteBlackboardData(W, BB, Refs, Known, Ctx.bVerboseMode);
            return;
        }
        if (UBlueprint* BP = Cast<UBlueprint>(Asset))
        {
            const TCHAR* Flavour = ResolveFlavour(BP);
            FBPeekBehaviorTreeWriter::WriteBTNodeBlueprint(W, BP, Flavour, Refs, Known, Ctx.bVerboseMode);
            return;
        }
    }

    void AppendToIndex(FBPeekIndexBuilder& IB, UObject* Asset) const override
    {
        // Route BT/BB assets through the Blueprint bucket for _index.md
        // visibility. A dedicated "Behavior Trees" bucket would need an
        // IndexBuilder API change — deferred until the feature has a
        // sibling (Animation BPs? AIController BPs?) that justifies one.
        if (UBlueprint* BP = Cast<UBlueprint>(Asset))
        {
            IB.AddBlueprint(BP);
        }
        // UBehaviorTree / UBlackboardData aren't UBlueprints — they would
        // need a new index bucket. Skip for now; they still get per-asset
        // MDs, just not grouped in _index.md under "Behavior Trees".
    }

private:
    static const TCHAR* ResolveFlavour(UBlueprint* BP)
    {
        if (!BP || !BP->ParentClass) return TEXT("BT Node Blueprint");
        for (UClass* P = BP->ParentClass; P; P = P->GetSuperClass())
        {
            if (P == UBTTaskNode::StaticClass())  return TEXT("BT Task Blueprint");
            if (P == UBTDecorator::StaticClass()) return TEXT("BT Decorator Blueprint");
            if (P == UBTService::StaticClass())   return TEXT("BT Service Blueprint");
        }
        return TEXT("BT Node Blueprint");
    }
};
