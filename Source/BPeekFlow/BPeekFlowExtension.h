#pragma once

#include "CoreMinimal.h"
#include "BPeekExtensionAPI.h"
#include "BPeekFlowWriter.h"
#include "BPeekIndexBuilder.h"
#include "BPeekAssetPathHelpers.h"
#include "BPeekMarkdownWriter.h"
#include "BPeekUsedBy.h"
#include "FlowAsset.h"
#include "Nodes/FlowNode.h"
#include "Nodes/FlowNodeBlueprint.h"
#include "Nodes/FlowPin.h"
#include "Engine/Blueprint.h"

//
// IBPeekExtension adapter for the Flow plugin. Handles two asset
// kinds:
//
//   1. UFlowAsset      — the scenario graph container. Renders via
//                        the existing FBPeekFlowWriter (nodes +
//                        mermaid + connection list).
//
//   2. UFlowNodeBlueprint — a Blueprint whose parent is a UFlowNode.
//                        These are standalone .uasset files that
//                        define reusable custom Flow nodes. Without
//                        this extension they'd fall through to the
//                        generic Blueprint writer, which knows
//                        nothing about FlowNode pin metadata.
//                        Rendering here reads the UFlowNode CDO
//                        (Category / deprecated flag / pin lists).
//
// UFlowNodeAddOn is intentionally NOT handled — user marked it as
// experimental and out of scope for the current cadence.
//
// Priority 200 beats core's generic Blueprint extension (100), so
// FN_* BPs land here instead of the plain Blueprint path.
//
class FBPeekFlowExtension : public IBPeekExtension
{
public:
    FName   GetId()          const override { return TEXT("bpeek.flow"); }
    FString GetVersionName() const override { return TEXT("1.0.0"); }
    int32   GetPriority()    const override { return 200; }

    TArray<UClass*> GetHandledClasses() const override
    {
        // UFlowNodeBlueprint is a UBlueprint subclass — core's
        // BlueprintExtension already iterates every UBlueprint via
        // TObjectIterator, so we don't need to list it here. FindFor
        // picks us up by priority when the asset's ParentClass is a
        // FlowNode descendant.
        return { UFlowAsset::StaticClass() };
    }

    bool CanHandle(UObject* Asset) const override
    {
        if (!Asset) return false;
        if (Asset->HasAnyFlags(RF_ClassDefaultObject)) return false;
        if (Cast<UFlowAsset>(Asset)) return true;

        // Flow node BP: walk the parent chain for UFlowNode.
        if (UBlueprint* BP = Cast<UBlueprint>(Asset))
        {
            for (UClass* P = BP->ParentClass; P; P = P->GetSuperClass())
                if (P == UFlowNode::StaticClass()) return true;
        }
        return false;
    }

    void Write(FBPeekMarkdownWriter& W, UObject* Asset, const FBPeekScanContext& Ctx) override
    {
        if (UFlowAsset* FA = Cast<UFlowAsset>(Asset))
        {
            WriteFlowAsset(W, FA, Ctx);
            return;
        }
        if (UBlueprint* BP = Cast<UBlueprint>(Asset))
        {
            WriteFlowNodeBlueprint(W, BP, Ctx);
            return;
        }
    }

    void AppendToIndex(FBPeekIndexBuilder& IB, UObject* Asset) const override
    {
        if (UFlowAsset* FA = Cast<UFlowAsset>(Asset))
        {
            FBPeekIndexBuilder::FEntry E;
            E.Name = FA->GetName();
            E.MdRelPath = FBPeekAssetPath::ToMdSubpath(FA->GetPathName());
            const int32 NodeCount = FA->GetNodes().Num();
            E.OneLiner = FString::Printf(TEXT("%d node%s"),
                NodeCount, NodeCount == 1 ? TEXT("") : TEXT("s"));
            IB.AddFlowEntry(MoveTemp(E));
            return;
        }
        if (UBlueprint* BP = Cast<UBlueprint>(Asset))
        {
            // Route custom Flow-node BPs through the generic Blueprint
            // bucket in _index.md for now. A separate "Flow Node BPs"
            // bucket would need IndexBuilder API changes; keeping a
            // single entry keeps discoverability without a schema bump.
            IB.AddBlueprint(BP);
        }
    }

private:
    static void WriteFlowAsset(FBPeekMarkdownWriter& W, UFlowAsset* FA, const FBPeekScanContext& Ctx)
    {
        static const TMap<FString, TArray<FString>> EmptyRefs;
        static const TSet<FString> EmptyKnown;
        FBPeekFlowWriter::Write(W, FA,
            Ctx.Refs  ? *Ctx.Refs  : EmptyRefs,
            Ctx.Known ? *Ctx.Known : EmptyKnown,
            Ctx.bVerboseMode);
    }

    static void WriteFlowNodeBlueprint(FBPeekMarkdownWriter& W, UBlueprint* BP, const FBPeekScanContext& Ctx)
    {
        const FString AssetPath = BP->GetPathName();
        const FString DisplayPath = Ctx.bVerboseMode
            ? AssetPath
            : FBPeekAssetPath::Compact(AssetPath);

        W.WriteHeading(1, FString::Printf(TEXT("%s (Flow Node Blueprint)"), *BP->GetName()));
        W.WriteLine();
        W.WriteMetaRowCode(TEXT("Asset path"), DisplayPath);
        if (BP->ParentClass)
            W.WriteMetaRowCode(TEXT("Parent"), BP->ParentClass->GetName());

        // The authored data lives on the GeneratedClass CDO — the
        // UFlowNode subclass this BP produced. An uncompiled BP or a
        // transient load can leave it null, so guard.
        UFlowNode* CDO = nullptr;
        if (BP->GeneratedClass)
            CDO = Cast<UFlowNode>(BP->GeneratedClass->GetDefaultObject());

        if (!CDO)
        {
            W.WriteLine();
            W.WriteLine(TEXT("_FlowNode CDO unavailable — Blueprint probably failed to compile._"));
            static const TMap<FString, TArray<FString>> EmptyRefs;
            static const TSet<FString> EmptyKnown;
            FBPeekUsedBy::Write(W,
                Ctx.Refs  ? *Ctx.Refs  : EmptyRefs, AssetPath,
                Ctx.Known ? *Ctx.Known : EmptyKnown);
            return;
        }

        const FString Category = CDO->GetNodeCategory();
        if (!Category.IsEmpty())
            W.WriteMetaRowCode(TEXT("Category"), Category);
        W.WriteLine();

        // Pin rendering uses FlowNode's protected arrays through a
        // friend accessor trick isn't needed: UPROPERTY makes them
        // reflection-visible. We use TFieldIterator + FProperty to
        // stay within the public surface.
        TArray<FString> InPinNames  = GetPinNamesViaReflection(CDO, TEXT("InputPins"));
        TArray<FString> OutPinNames = GetPinNamesViaReflection(CDO, TEXT("OutputPins"));

        EmitPinSection(W, TEXT("Input pins"),  InPinNames,  Ctx.bVerboseMode);
        EmitPinSection(W, TEXT("Output pins"), OutPinNames, Ctx.bVerboseMode);

        // Optional: deprecation flag.
        if (FBoolProperty* Dep = FindFProperty<FBoolProperty>(UFlowNode::StaticClass(), TEXT("bNodeDeprecated")))
        {
            if (Dep->GetPropertyValue_InContainer(CDO))
            {
                W.WriteLine(TEXT("_**Deprecated** — marked `bNodeDeprecated = true` on the node CDO._"));
                W.WriteLine();
            }
        }

        static const TMap<FString, TArray<FString>> EmptyRefs;
        static const TSet<FString> EmptyKnown;
        FBPeekUsedBy::Write(W,
            Ctx.Refs  ? *Ctx.Refs  : EmptyRefs, AssetPath,
            Ctx.Known ? *Ctx.Known : EmptyKnown);
    }

    /** Read InputPins / OutputPins TArray<FFlowPin> from a FlowNode CDO
     *  by reflection. Avoids a friend-class dance and keeps the
     *  accessor future-proof across Flow plugin versions. */
    static TArray<FString> GetPinNamesViaReflection(UFlowNode* CDO, const TCHAR* FieldName)
    {
        TArray<FString> Out;
        FArrayProperty* ArrProp = FindFProperty<FArrayProperty>(UFlowNode::StaticClass(), FieldName);
        if (!ArrProp) return Out;
        FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>(CDO));
        // Each element is FFlowPin. PinName is FName at offset 0 of
        // FFlowPin; we go through the inner struct property instead of
        // hardcoding the offset.
        FStructProperty* Inner = CastField<FStructProperty>(ArrProp->Inner);
        if (!Inner) return Out;
        FNameProperty* PinNameProp = FindFProperty<FNameProperty>(Inner->Struct, TEXT("PinName"));
        if (!PinNameProp) return Out;
        for (int32 i = 0; i < Helper.Num(); ++i)
        {
            void* Elem = Helper.GetRawPtr(i);
            const FName N = PinNameProp->GetPropertyValue(PinNameProp->ContainerPtrToValuePtr<void>(Elem));
            Out.Add(N.ToString());
        }
        return Out;
    }

    static void EmitPinSection(FBPeekMarkdownWriter& W, const FString& Label,
                               const TArray<FString>& Pins, bool bVerboseMode)
    {
        if (Pins.Num() == 0) return;
        if (bVerboseMode)
        {
            W.WriteHeading(2, FString::Printf(TEXT("%s (%d)"), *Label, Pins.Num()));
            W.WriteLine();
            for (const FString& P : Pins)
                W.WriteLine(FString::Printf(TEXT("- `%s`"), *P));
            W.WriteLine();
        }
        else
        {
            TArray<FString> Q;
            Q.Reserve(Pins.Num());
            for (const FString& P : Pins) Q.Add(FString::Printf(TEXT("`%s`"), *P));
            W.WriteLine(FString::Printf(TEXT("**%s (%d):** %s"),
                *Label, Pins.Num(), *FString::Join(Q, TEXT(", "))));
            W.WriteLine();
        }
    }
};
