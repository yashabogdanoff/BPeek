#pragma once

#include "CoreMinimal.h"
#include "BPeekExtensionAPI.h"
#include "BPeekEnumWriter.h"
#include "BPeekStructWriter.h"
#include "BPeekDataTableWriter.h"
#include "BPeekBlueprintWriter.h"
#include "BPeekLevelWriter.h"
#include "BPeekLevelSequenceWriter.h"
#include "BPeekDataAssetWriter.h"
#include "BPeekIndexBuilder.h"
#include "BPeekGraphWalker.h"
#include "Engine/UserDefinedEnum.h"
#include "BPeekCompat.h"
#include "Engine/DataTable.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Engine/DataAsset.h"
#include "LevelSequence.h"

//
// Built-in IBPeekExtension adapters for the engine-native asset types.
//
// Each adapter is a thin wrapper around a static Write() helper: the
// adapter's CanHandle() checks the UClass, and Write() delegates to
// the helper. Having every asset type go through the same IBPeekExtension
// dispatch keeps the scanner free of any special case for "internal"
// vs "external" renderers.
//
// Registered by FBPeekModule::StartupModule. Not meant to be
// instantiated manually outside that.
//

namespace BPeekCoreExtensions
{
    /** Empty fallback so extensions can call Write() with references
     *  even when the scanner hasn't filled Ctx.Refs / Ctx.Known. */
    inline const TMap<FString, TArray<FString>>& EmptyRefs()
    {
        static const TMap<FString, TArray<FString>> Empty;
        return Empty;
    }
    inline const TSet<FString>& EmptyKnown()
    {
        static const TSet<FString> Empty;
        return Empty;
    }
}

class FBPeekEnumExtension : public IBPeekExtension
{
public:
    FName GetId() const override { return TEXT("bpeek.core.enum"); }
    FString GetVersionName() const override { return TEXT("1.0.0"); }
    int32 GetPriority() const override { return 100; }

    TArray<UClass*> GetHandledClasses() const override
    {
        return { UUserDefinedEnum::StaticClass() };
    }
    bool CanHandle(UObject* Asset) const override
    {
        return Cast<UUserDefinedEnum>(Asset) != nullptr;
    }
    void Write(FBPeekMarkdownWriter& W, UObject* Asset, const FBPeekScanContext& Ctx) override
    {
        UEnum* E = Cast<UUserDefinedEnum>(Asset);
        if (!E) return;
        FBPeekEnumWriter::Write(W, E, Ctx.UassetRel,
            Ctx.Refs  ? *Ctx.Refs  : BPeekCoreExtensions::EmptyRefs(),
            Ctx.Known ? *Ctx.Known : BPeekCoreExtensions::EmptyKnown(),
            Ctx.bVerboseMode);
    }
    void AppendToIndex(FBPeekIndexBuilder& IB, UObject* Asset) const override
    {
        if (UEnum* E = Cast<UUserDefinedEnum>(Asset)) IB.AddEnum(E);
    }
};

class FBPeekStructExtension : public IBPeekExtension
{
public:
    FName GetId() const override { return TEXT("bpeek.core.struct"); }
    FString GetVersionName() const override { return TEXT("1.0.0"); }
    int32 GetPriority() const override { return 100; }

    TArray<UClass*> GetHandledClasses() const override
    {
        return { UUserDefinedStruct::StaticClass() };
    }
    bool CanHandle(UObject* Asset) const override
    {
        return Cast<UUserDefinedStruct>(Asset) != nullptr;
    }
    void Write(FBPeekMarkdownWriter& W, UObject* Asset, const FBPeekScanContext& Ctx) override
    {
        UUserDefinedStruct* S = Cast<UUserDefinedStruct>(Asset);
        if (!S) return;
        FBPeekStructWriter::Write(W, S, Ctx.UassetRel,
            Ctx.Refs  ? *Ctx.Refs  : BPeekCoreExtensions::EmptyRefs(),
            Ctx.Known ? *Ctx.Known : BPeekCoreExtensions::EmptyKnown(),
            Ctx.bVerboseMode);
    }
    void AppendToIndex(FBPeekIndexBuilder& IB, UObject* Asset) const override
    {
        if (UUserDefinedStruct* S = Cast<UUserDefinedStruct>(Asset)) IB.AddStruct(S);
    }
};

class FBPeekDataTableExtension : public IBPeekExtension
{
public:
    FName GetId() const override { return TEXT("bpeek.core.datatable"); }
    FString GetVersionName() const override { return TEXT("1.0.0"); }
    int32 GetPriority() const override { return 100; }

    TArray<UClass*> GetHandledClasses() const override
    {
        return { UDataTable::StaticClass() };
    }
    bool CanHandle(UObject* Asset) const override
    {
        return Cast<UDataTable>(Asset) != nullptr;
    }
    void Write(FBPeekMarkdownWriter& W, UObject* Asset, const FBPeekScanContext& Ctx) override
    {
        UDataTable* DT = Cast<UDataTable>(Asset);
        if (!DT) return;
        FBPeekDataTableWriter::Write(W, DT, Ctx.UassetRel,
            Ctx.Refs  ? *Ctx.Refs  : BPeekCoreExtensions::EmptyRefs(),
            Ctx.Known ? *Ctx.Known : BPeekCoreExtensions::EmptyKnown(),
            Ctx.bVerboseMode);
    }
    void AppendToIndex(FBPeekIndexBuilder& IB, UObject* Asset) const override
    {
        if (UDataTable* DT = Cast<UDataTable>(Asset)) IB.AddDataTable(DT);
    }
};

/** Covers UBlueprint (including UWidgetBlueprint and UAnimBlueprint
 *  subclasses — UBlueprint::StaticClass() catches all three via
 *  TObjectIterator, and BlueprintWriter branches on the concrete type
 *  internally). Return-value stats from BlueprintWriter::Write are
 *  discarded in the extension path — stats aggregation is a scanner
 *  concern that'll plug in via a later ScanContext callback when the
 *  commandlet migrates to registry-dispatch. */
class FBPeekBlueprintExtension : public IBPeekExtension
{
public:
    FName GetId() const override { return TEXT("bpeek.core.blueprint"); }
    FString GetVersionName() const override { return TEXT("1.0.0"); }
    int32 GetPriority() const override { return 100; }

    TArray<UClass*> GetHandledClasses() const override
    {
        return { UBlueprint::StaticClass() };
    }
    bool CanHandle(UObject* Asset) const override
    {
        UBlueprint* BP = Cast<UBlueprint>(Asset);
        if (!BP) return false;
        // Sub-object BPs (LevelScript, DirectorBP) are nested inside
        // their owning map/sequence and aren't standalone assets.
        if (BP->GetPathName().Contains(TEXT(":"))) return false;
        return true;
    }
    void Write(FBPeekMarkdownWriter& W, UObject* Asset, const FBPeekScanContext& Ctx) override
    {
        UBlueprint* BP = Cast<UBlueprint>(Asset);
        if (!BP) return;

        // Default (AI-optimised) splits the Logic section off into
        // <name>.logic.md so the main MD is tiny and the agent only
        // pulls logic on demand. Verbose mode keeps everything in one
        // file. The "see also" link is emitted by BlueprintWriter when
        // LogicOverride is non-null.
        if (!Ctx.bVerboseMode && !Ctx.MdPath.IsEmpty())
        {
            FBPeekMarkdownWriter LogicW;
            FBPeekCoverageStats Stats = FBPeekBlueprintWriter::Write(W, BP,
                Ctx.Refs  ? *Ctx.Refs  : BPeekCoreExtensions::EmptyRefs(),
                Ctx.Known ? *Ctx.Known : BPeekCoreExtensions::EmptyKnown(),
                /*bVerboseMode=*/ false, &LogicW);
            if (Ctx.CoverageOut) *Ctx.CoverageOut += Stats;

            // Derive "<...>/X.md" → "<...>/X.logic.md". Strip the
            // trailing ".md" (3 chars) and append the logic suffix.
            FString LogicPath = Ctx.MdPath;
            if (LogicPath.EndsWith(TEXT(".md"))) LogicPath = LogicPath.LeftChop(3);
            LogicPath += TEXT(".logic.md");
            LogicW.SaveTo(LogicPath);
            return;
        }

        FBPeekCoverageStats Stats = FBPeekBlueprintWriter::Write(W, BP,
            Ctx.Refs  ? *Ctx.Refs  : BPeekCoreExtensions::EmptyRefs(),
            Ctx.Known ? *Ctx.Known : BPeekCoreExtensions::EmptyKnown(),
            Ctx.bVerboseMode);
        if (Ctx.CoverageOut) *Ctx.CoverageOut += Stats;
    }
    void AppendToIndex(FBPeekIndexBuilder& IB, UObject* Asset) const override
    {
        if (UBlueprint* BP = Cast<UBlueprint>(Asset)) IB.AddBlueprint(BP);
    }
};

class FBPeekLevelExtension : public IBPeekExtension
{
public:
    FName GetId() const override { return TEXT("bpeek.core.level"); }
    FString GetVersionName() const override { return TEXT("1.0.0"); }
    int32 GetPriority() const override { return 100; }

    TArray<UClass*> GetHandledClasses() const override
    {
        return { UWorld::StaticClass() };
    }
    bool CanHandle(UObject* Asset) const override
    {
        UWorld* W = Cast<UWorld>(Asset);
        if (!W || !W->PersistentLevel) return false;
        // /Temp/ is used for PIE and editor-internal synthesis; never
        // emit MD for those.
        if (W->GetPathName().StartsWith(TEXT("/Temp/"))) return false;
        return true;
    }
    void Write(FBPeekMarkdownWriter& W, UObject* Asset, const FBPeekScanContext& Ctx) override
    {
        UWorld* World = Cast<UWorld>(Asset);
        if (!World) return;
        FBPeekLevelWriter::Write(W, World,
            Ctx.Refs  ? *Ctx.Refs  : BPeekCoreExtensions::EmptyRefs(),
            Ctx.Known ? *Ctx.Known : BPeekCoreExtensions::EmptyKnown(),
            Ctx.bVerboseMode);
    }
    void AppendToIndex(FBPeekIndexBuilder& IB, UObject* Asset) const override
    {
        if (UWorld* W = Cast<UWorld>(Asset)) IB.AddLevel(W);
    }
};

class FBPeekLevelSequenceExtension : public IBPeekExtension
{
public:
    FName GetId() const override { return TEXT("bpeek.core.levelsequence"); }
    FString GetVersionName() const override { return TEXT("1.0.0"); }
    int32 GetPriority() const override { return 100; }

    TArray<UClass*> GetHandledClasses() const override
    {
        return { ULevelSequence::StaticClass() };
    }
    bool CanHandle(UObject* Asset) const override
    {
        ULevelSequence* LS = Cast<ULevelSequence>(Asset);
        return LS && LS->GetMovieScene() != nullptr;
    }
    void Write(FBPeekMarkdownWriter& W, UObject* Asset, const FBPeekScanContext& Ctx) override
    {
        ULevelSequence* LS = Cast<ULevelSequence>(Asset);
        if (!LS) return;
        FBPeekLevelSequenceWriter::Write(W, LS,
            Ctx.Refs  ? *Ctx.Refs  : BPeekCoreExtensions::EmptyRefs(),
            Ctx.Known ? *Ctx.Known : BPeekCoreExtensions::EmptyKnown(),
            Ctx.bVerboseMode);
    }
    void AppendToIndex(FBPeekIndexBuilder& IB, UObject* Asset) const override
    {
        if (ULevelSequence* LS = Cast<ULevelSequence>(Asset)) IB.AddLevelSequence(LS);
    }
};

/** Generic fallback for any UDataAsset that no specific extension
 *  claims. Priority 0 — every typed extension (Flow, GAS, Input,
 *  Blueprint, ...) outranks it and takes the asset instead. */
class FBPeekDataAssetExtension : public IBPeekExtension
{
public:
    FName GetId() const override { return TEXT("bpeek.core.dataasset"); }
    FString GetVersionName() const override { return TEXT("1.0.0"); }
    int32 GetPriority() const override { return 0; }

    TArray<UClass*> GetHandledClasses() const override
    {
        return { UDataAsset::StaticClass() };
    }
    bool CanHandle(UObject* Asset) const override
    {
        UDataAsset* DA = Cast<UDataAsset>(Asset);
        if (!DA) return false;
        if (DA->HasAnyFlags(RF_ClassDefaultObject)) return false;
        return true;
    }
    void Write(FBPeekMarkdownWriter& W, UObject* Asset, const FBPeekScanContext& Ctx) override
    {
        UDataAsset* DA = Cast<UDataAsset>(Asset);
        if (!DA) return;
        FBPeekDataAssetWriter::Write(W, DA,
            Ctx.Refs  ? *Ctx.Refs  : BPeekCoreExtensions::EmptyRefs(),
            Ctx.Known ? *Ctx.Known : BPeekCoreExtensions::EmptyKnown(),
            Ctx.bVerboseMode);
    }
    void AppendToIndex(FBPeekIndexBuilder& IB, UObject* Asset) const override
    {
        if (UDataAsset* DA = Cast<UDataAsset>(Asset)) IB.AddDataAsset(DA);
    }
};

// Flow rendering lives in the BPeekFlow submodule so core can stay
// free of any link-time dependency on the community Flow plugin
// (github.com/MothCocoon/FlowGraph). FlowAsset.h is not included
// here and "Flow" is not declared as a core module dep.
