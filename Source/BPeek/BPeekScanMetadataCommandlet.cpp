#include "BPeekScanMetadataCommandlet.h"
#include "BPeekLog.h"
#include "Misc/ScopedSlowTask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/DataTable.h"
#include "Engine/UserDefinedEnum.h"
#include "BPeekCompat.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Animation/WidgetAnimation.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "EdGraph/EdGraph.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "HAL/FileManager.h"
#include "BPeekMarkdownWriter.h"
#include "BPeekTextUnwrap.h"
#include "BPeekAssetPathHelpers.h"
#include "BPeekAssetLinks.h"
#include "BPeekUsedBy.h"
#include "BPeekEnumWriter.h"
#include "BPeekStructWriter.h"
#include "BPeekDataTableWriter.h"
#include "BPeekBlueprintWriter.h"
#include "BPeekGameplayTagsWriter.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "Engine/StaticMeshActor.h"
#include "BPeekLevelWriter.h"
// Core doesn't depend on the Flow plugin's module — rendering for
// UFlowAsset lives in the BPeekFlow submodule. Flow string names are
// still listed in RenderableLeafNames / RenderableBases below so the
// Asset Registry still flags Flow assets as renderable and the
// BPeekFlow extension can pick them up through the registry.
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneTrack.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSpawnable.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_CallFunction.h"
#include "BPeekLevelSequenceWriter.h"
#include "Engine/DataAsset.h"
#include "BPeekDataAssetWriter.h"
#include "BPeekIndexBuilder.h"
#include "BPeekHashStore.h"
#include "BPeekCppSource.h"
#include "BPeekPathFilter.h"
#include "BPeekSettings.h"
#include "BPeekExtensionAPI.h"
#include "BPeekExtensionRegistry.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectHash.h"
#include "Misc/App.h"

// Converts a UE asset path like "/Game/X/Y.Y" into a filesystem-safe
// subpath "X/Y.md" (no double-escape, preserves Unicode). Used to
// mirror the mount-point tree under the output directory.
static FString BPeekAssetPathToMdSubpath(const FString& AssetPath)
{
    FString Rel = AssetPath;
    if (Rel.StartsWith(TEXT("/"))) Rel = Rel.RightChop(1);
    int32 DotIdx;
    if (Rel.FindLastChar(TEXT('.'), DotIdx)) Rel = Rel.Left(DotIdx);
    Rel.ReplaceInline(TEXT("\\"), TEXT("/"));
    return Rel + TEXT(".md");
}

static FString PinTypeToStr(const FEdGraphPinType& T)
{
    FString Base = T.PinCategory.ToString();
    if (T.PinSubCategoryObject.IsValid()) Base = T.PinSubCategoryObject->GetName();
    if (T.ContainerType == EPinContainerType::Array) return FString::Printf(TEXT("TArray<%s>"), *Base);
    if (T.ContainerType == EPinContainerType::Set) return FString::Printf(TEXT("TSet<%s>"), *Base);
    if (T.ContainerType == EPinContainerType::Map) {
        FString Val = T.PinValueType.TerminalCategory.ToString();
        if (T.PinValueType.TerminalSubCategoryObject.IsValid()) Val = T.PinValueType.TerminalSubCategoryObject->GetName();
        return FString::Printf(TEXT("TMap<%s, %s>"), *Base, *Val);
    }
    return Base;
}

static TSharedPtr<FJsonObject> DumpWidget(UWidget* W) {
    if (!W) return nullptr;
    auto O = MakeShared<FJsonObject>();
    O->SetStringField(TEXT("name"), W->GetName());
    O->SetStringField(TEXT("class"), W->GetClass()->GetName());

    // Editable UPROPERTY values — Text у TextBlock, Brush у Image, Font, Alignments,
    // цвета и т.п. Skip базовые UWidget/UPanelWidget plumbing (Slot, bIsEnabled, Navigation,
    // ToolTipText movement-через-widget-стиль) — они редко несут смысловую нагрузку для AI.
    auto Props = MakeShared<FJsonObject>();
    for (TFieldIterator<FProperty> PIt(W->GetClass()); PIt; ++PIt) {
        FProperty* P = *PIt;
        if (!P->HasAnyPropertyFlags(CPF_Edit)) continue;
        if (P->HasAnyPropertyFlags(CPF_Transient)) continue;
        UClass* Owner = P->GetOwnerClass();
        if (Owner == UWidget::StaticClass()) continue;
        if (Owner == UPanelWidget::StaticClass()) continue;
        const FString PropName = P->GetName();
        // Skip bOverride_* paired flags that are off — they paint the tree with noise.
        if (PropName.StartsWith(TEXT("bOverride_"))) {
            if (FBoolProperty* BP = CastField<FBoolProperty>(P);
                BP && !BP->GetPropertyValue_InContainer(W)) continue;
        }
        if (FObjectProperty* OP = CastField<FObjectProperty>(P)) {
            if (UObject* V = OP->GetObjectPropertyValue_InContainer(W))
                Props->SetStringField(PropName, V->GetPathName());
            continue;
        }
        if (FSoftObjectProperty* SP = CastField<FSoftObjectProperty>(P)) {
            const FSoftObjectPtr V = SP->GetPropertyValue_InContainer(W);
            if (!V.IsNull()) Props->SetStringField(PropName, V.ToString());
            continue;
        }
        if (FTextProperty* TP = CastField<FTextProperty>(P)) {
            const FText V = TP->GetPropertyValue_InContainer(W);
            if (!V.IsEmpty()) Props->SetStringField(PropName, V.ToString());
            continue;
        }
        FString Val;
        P->ExportTextItem_Direct(Val, P->ContainerPtrToValuePtr<void>(W), nullptr, nullptr, PPF_None);
        if (!Val.IsEmpty() && Val != TEXT("None") && Val != TEXT("\"\"") && Val != TEXT("()"))
            Props->SetStringField(PropName, Val);
    }
    if (Props->Values.Num() > 0) O->SetObjectField(TEXT("properties"), Props);

    if (UPanelWidget* P = Cast<UPanelWidget>(W)) {
        TArray<TSharedPtr<FJsonValue>> Ch;
        for (int32 i = 0; i < P->GetChildrenCount(); i++)
            if (auto C = DumpWidget(P->GetChildAt(i))) Ch.Add(MakeShared<FJsonValueObject>(C));
        if (Ch.Num() > 0) O->SetArrayField(TEXT("children"), Ch);
    }
    return O;
}

UBPeekScanMetadataCommandlet::UBPeekScanMetadataCommandlet() { IsClient=false; IsEditor=true; IsServer=false; LogToConsole=true; }

int32 UBPeekScanMetadataCommandlet::Main(const FString& Params)
{
    FString OutputPath;
    if (!FParse::Value(*Params, TEXT("-output="), OutputPath))
        OutputPath = FPaths::ProjectDir() / TEXT("bpeek-metadata.json");
    OutputPath = FPaths::ConvertRelativePathToFull(OutputPath);

    // Settings backing: Project Settings > Plugins > BPeek. Used as the
    // fallback for fields when the corresponding CLI flag isn't supplied.
    const UBPeekSettings* Settings = GetDefault<UBPeekSettings>();

    // Optional MD pilot output — parallel to the C# renderer. Empty = disabled.
    // Precedence: CLI > UBPeekSettings > hard-coded <Project>/Saved/BPeek.
    FString BPeekMdDir;
    FParse::Value(*Params, TEXT("-bpeekmd="), BPeekMdDir);
    if (BPeekMdDir.IsEmpty() && Settings)
        BPeekMdDir = Settings->ResolveOutputDirectory();
    if (!BPeekMdDir.IsEmpty())
        BPeekMdDir = FPaths::ConvertRelativePathToFull(BPeekMdDir);

    // Compute depth between project root and MD output tree so cpp-ref
    // [Source] links use the right number of `../` prefixes. Saved/BPeek/
    // = 2, bpeek/ = 1, Content/bpeek/ = 2, custom outside project = best
    // effort (links will be informative but not click-resolvable).
    if (!BPeekMdDir.IsEmpty())
    {
        const FString ProjectFull = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
        FString Rel = BPeekMdDir;
        if (Rel.StartsWith(ProjectFull)) Rel = Rel.RightChop(ProjectFull.Len());
        while (Rel.StartsWith(TEXT("/")) || Rel.StartsWith(TEXT("\\"))) Rel = Rel.RightChop(1);
        int32 Depth = Rel.IsEmpty() ? 0 : 1;
        for (TCHAR C : Rel) if (C == TEXT('/') || C == TEXT('\\')) ++Depth;
        FBPeekCppSource::OutputDirDepth = Depth;
        UE_LOG(LogBPeek, Display,
            TEXT("[BPeekScan] Output depth under project root: %d (for cpp-ref links)"), Depth);
    }

    // Optional ';'-separated UE-path prefix whitelist for the MD pilot.
    // Only assets whose GetPathName() starts with one of these prefixes get
    // a .md emitted. Empty list = no filtering, write MDs for every asset.
    TArray<FString> BPeekMdFilters;
    {
        FString BPeekMdFilterRaw;
        if (FParse::Value(*Params, TEXT("-bpeekmdfilter="), BPeekMdFilterRaw) && !BPeekMdFilterRaw.IsEmpty())
            BPeekMdFilterRaw.ParseIntoArray(BPeekMdFilters, TEXT(";"), /*InCullEmpty=*/true);
    }

    // Optional `,`-separated exact-match whitelist. Highest-priority filter:
    // when non-empty, only assets whose normalised path is in the set get
    // a MD. Wired from the editor context-menu handler ("Dump for this asset")
    // so right-clicking N assets → dumps exactly those N without scanning
    // the rest of the project.
    TSet<FString> BPeekAssetExactSet;
    {
        FString AssetExactRaw;
        if (FParse::Value(*Params, TEXT("-asset="), AssetExactRaw) && !AssetExactRaw.IsEmpty())
        {
            TArray<FString> Parts;
            AssetExactRaw.ParseIntoArray(Parts, TEXT(","), /*bCullEmpty=*/true);
            for (const FString& P : Parts)
            {
                // Normalise so both "/Game/X/BP" and "/Game/X/BP.BP" match.
                BPeekAssetExactSet.Add(FBPeekAssetPath::Normalize(P));
            }
            UE_LOG(LogBPeek, Display,
                TEXT("[BPeekScan] -asset= exact-match filter: %d asset(s)"),
                BPeekAssetExactSet.Num());
        }
    }

    // UBPeekSettings include/exclude glob filter. Loaded from Project
    // Settings > Plugins > BPeek. IncludePatterns/ExcludePatterns are
    // TArray<FString> (one pattern per element, idiomatic for ini `+`
    // multi-value syntax) — join into newline-separated blob the filter
    // Load() already expects. External files, if configured, replace
    // the in-settings patterns (last-call-wins inside FBPeekPathFilter).
    // Takes lowest priority — CLI `-asset=` and `-bpeekmdfilter=`
    // still win when supplied.
    FBPeekPathFilter SettingsFilter;
    if (Settings)
    {
        if (Settings->IncludePatterns.Num() > 0)
            SettingsFilter.LoadInclude(FString::Join(Settings->IncludePatterns, TEXT("\n")));
        if (Settings->ExcludePatterns.Num() > 0)
            SettingsFilter.LoadExclude(FString::Join(Settings->ExcludePatterns, TEXT("\n")));
        if (!Settings->ExternalIncludeFile.FilePath.IsEmpty())
            SettingsFilter.LoadIncludeFromFile(Settings->ExternalIncludeFile.FilePath);
        if (!Settings->ExternalExcludeFile.FilePath.IsEmpty())
            SettingsFilter.LoadExcludeFromFile(Settings->ExternalExcludeFile.FilePath);
        if (SettingsFilter.IsActive())
        {
            UE_LOG(LogBPeek, Display,
                TEXT("[BPeekScan] UBPeekSettings filter: %d include, %d exclude rule(s)"),
                SettingsFilter.NumInclude(), SettingsFilter.NumExclude());
        }
    }

    auto BPeekMdWanted = [&BPeekMdFilters, &BPeekAssetExactSet, &SettingsFilter](const FString& AssetPath) -> bool
    {
        if (BPeekAssetExactSet.Num() > 0)
            return BPeekAssetExactSet.Contains(FBPeekAssetPath::Normalize(AssetPath));
        if (BPeekMdFilters.Num() > 0)
        {
            for (const FString& Pref : BPeekMdFilters)
                if (AssetPath.StartsWith(Pref)) return true;
            return false;
        }
        if (SettingsFilter.IsActive())
            return SettingsFilter.ShouldInclude(AssetPath);
        return true;
    };

    // Known-path set is built after the AssetRegistry loads everything,
    // just before the main write loops (see further down).
    TSet<FString> BPeekKnownNormalized;

    UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Starting metadata dump, output=%s"), *OutputPath);

    // Progress reporting. When invoked from the editor module (toolbar /
    // context-menu callbacks), those wrap the call in GWarn->BeginSlowTask
    // with bShowProgress=true; each StatusUpdate call pumps Slate so the
    // dialog's text AND progress bar actually advance. When invoked from
    // the commandline, GWarn is a console output device — the StatusUpdate
    // calls still work (log lines) but don't draw a dialog.
    //
    // Phases (total [0..100]):
    //    0– 5: SearchAllAssets (AR may push its own nested slow task).
    //    5–50: load pass (iterate AR + LoadObject, per-16-asset pump).
    //   50–90: MD writes. Biggest chunk is Blueprints; pump per-BP with
    //          fraction inside the 50-90 band.
    //   90–95: Level / Flow / LevelSequence / DataAsset writes.
    //   95–100: refs scan, _index.md, hash save.
    //
    // Diagnostic: every update is mirrored at Verbose-level so if the
    // dialog shows a value we didn't submit, the log shows what we
    // actually did submit.
    // Progress driven by FScopedSlowTask with ESlowTaskVisibility::Important
    // so the outer scan fraction isn't shadowed by the inner tasks
    // FAssetData::GetAsset() pushes during package loading ("Loading
    // package X", 1/1 = 100%). Without Important visibility the user
    // would see 100% / 100% / 100% jitter during the load phase.
    //
    // ReportProgress tracks the running cumulative percent and converts to
    // the FScopedSlowTask's Delta-based EnterProgressFrame. Same log line
    // format as before for easy before/after comparison.
    TUniquePtr<FScopedSlowTask> OverallTask(
        new FScopedSlowTask(100.0f,
            FText::FromString(TEXT("BPeek: dumping project..."))));
    OverallTask->Visibility = ESlowTaskVisibility::Important;
    OverallTask->MakeDialog(/*bShowCancelButton*/ false, /*bAllowInPIE*/ false);

    float CurrentPct = 0.0f;
    // Float overload — used by the per-asset pumps which need sub-percent
    // resolution (pumping 954 times across a 45-pt band = ~0.047 pt each).
    auto ReportProgressF = [&OverallTask, &CurrentPct](float PercentTarget, const FString& Msg)
    {
        PercentTarget = FMath::Clamp(PercentTarget, 0.0f, 100.0f);
        const float Delta = FMath::Max(0.0f, PercentTarget - CurrentPct);
        OverallTask->EnterProgressFrame(Delta, FText::FromString(Msg));
        CurrentPct = FMath::Max(CurrentPct, PercentTarget);
        // Verbose: one line per asset during load + BP write — a typical
        // ~1000-asset project produces ~2k lines. Off by default; opt
        // in via `-LogCmds="LogBPeek Verbose"` on the commandline or
        // `[Core.Log] LogBPeek=Verbose` in DefaultEngine.ini.
        UE_LOG(LogBPeek, Verbose, TEXT("[progress] %5.1f%% | %s"), PercentTarget, *Msg);
    };
    // Int wrapper for the phase-marker calls (5, 50, 85, 90, 95, 100).
    // Phase markers stay at Log so a user reading Output Log sees the
    // high-level flow without bumping verbosity.
    auto ReportProgress = [&ReportProgressF](int32 PercentTarget, const FString& Msg)
    {
        ReportProgressF(float(PercentTarget), Msg);
        UE_LOG(LogBPeek, Log, TEXT("[progress] %3d%% | %s"), PercentTarget, *Msg);
    };
    UE_LOG(LogBPeek, Log, TEXT("[progress] FScopedSlowTask Important, editor=%d commandlet=%d"),
        GIsEditor ? 1 : 0, IsRunningCommandlet() ? 1 : 0);

    // Initial text/position. Most nested engine slow tasks during AR scan
    // won't overlay because we're marked Important.
    ReportProgress(0, TEXT("BPeek: scanning asset registry..."));

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    ARM.Get().SearchAllAssets(true);
    TArray<FAssetData> All; ARM.Get().GetAllAssets(All, true);
    UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] AssetRegistry returned %d total assets"), All.Num());

    // Reassert our progress state post-scan — AR may have stomped the
    // dialog with its own StatusUpdate calls during SearchAllAssets.
    ReportProgress(5, FString::Printf(
        TEXT("BPeek: scanned %d assets, filtering..."), All.Num()));

    int32 Loaded = 0, Skipped = 0, LoadErrors = 0;
    if (BPeekAssetExactSet.Num() > 0)
    {
        // Per-asset fast path — load ONLY the requested assets, not the
        // whole project. Avoids triggering load of 16k+ assets and their
        // shader compile cascades for a single-BP context-menu dump.
        // Cross-ref data (Referenced by) is skipped for these runs since
        // we haven't scanned potential referrers; the per-asset MDs show
        // an empty Referenced-by section, which is semantically correct
        // for single-asset scope.
        for (const FString& Norm : BPeekAssetExactSet)
        {
            // BPeekAssetExactSet was stored in normalised form (no .Name
            // suffix). FSoftObjectPath/LoadObject need the full form to
            // resolve unambiguously — rebuild as "<path>.<name>".
            FString FullPath = Norm;
            int32 LastSlash;
            if (FullPath.FindLastChar(TEXT('/'), LastSlash))
                FullPath += TEXT(".") + FullPath.Mid(LastSlash + 1);
            UObject* Obj = LoadObject<UObject>(nullptr, *FullPath);
            if (Obj) { ++Loaded; }
            else { ++LoadErrors; UE_LOG(LogBPeek, Warning, TEXT("[BPeekScan] Failed to load: %s"), *FullPath); }
        }
        UE_LOG(LogBPeek, Display,
            TEXT("[BPeekScan] Per-asset mode: loaded %d of %d requested assets (skipping full project scan)"),
            Loaded, BPeekAssetExactSet.Num());
    }
    else
    {
        // Class-level gate — only load assets that BPeek actually renders
        // to markdown. Without this, a /Game/** include rule pulls in
        // every material / texture / static mesh (tens of thousands on
        // a typical project) which then cascades into shader compiles
        // we never needed. The MD-write loops further down already
        // iterate type-specific TObjectIterators, so loading anything
        // outside these classes is pure waste.
        static const TSet<FName> RenderableClassNames = {
            FName(TEXT("Blueprint")),
            FName(TEXT("WidgetBlueprint")),
            FName(TEXT("AnimBlueprint")),
            FName(TEXT("UserDefinedEnum")),
            FName(TEXT("UserDefinedStruct")),
            FName(TEXT("DataTable")),
            FName(TEXT("FlowAsset")),
            FName(TEXT("World")),            // ULevel / Maps
            FName(TEXT("LevelSequence")),
        };
        // DataAsset subclasses are project-defined and not enumerable
        // by hardcoding. Ask AR for the full derived class set once and
        // let the load loop consult it.
        TSet<FTopLevelAssetPath> DataAssetClassPaths;
        ARM.Get().GetDerivedClassNames(
            { FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("DataAsset")) },
            TSet<FTopLevelAssetPath>(), DataAssetClassPaths);

        auto IsRenderableClass = [&DataAssetClassPaths](const FAssetData& A) -> bool
        {
            if (RenderableClassNames.Contains(A.AssetClassPath.GetAssetName()))
                return true;
            if (DataAssetClassPaths.Contains(A.AssetClassPath))
                return true;
            return false;
        };

        // Two-pass: first pass counts filter-passing assets so the second
        // pass can emit real fraction-based progress. Cheap string scan
        // (no load), so negligible vs the load cascade it tracks.
        int32 TotalWanted = 0;
        for (const FAssetData& A : All) {
            FString P = A.GetObjectPathString();
            if (P.StartsWith(TEXT("/Script/")) || P.Contains(TEXT("/Saved/"))) continue;
            if (!IsRenderableClass(A)) continue;
            if (!BPeekMdWanted(P)) continue;
            ++TotalWanted;
        }
        UE_LOG(LogBPeek, Display,
            TEXT("[BPeekScan] renderable + filter-passing: %d of %d AR assets"),
            TotalWanted, All.Num());
        UE_LOG(LogBPeek, Display,
            TEXT("[BPeekScan] %d filter-passing assets will be loaded"), TotalWanted);

        // Full-project and folder-scope paths both run here. Filter is
        // applied at LOAD stage (not just MD-write) so that a folder-scope
        // prefix or an UBPeekSettings exclude-list actually prevents the
        // matching .uasset from being loaded.
        //
        // Hard skips (/Script, /Saved) bypass the filter entirely —
        // these paths are never loadable assets. /Engine itself is no
        // longer hardcoded — if the user wants engine assets in their
        // dump, clearing /Engine/** from Exclude Patterns is enough.
        int32 Processed = 0;
        for (const FAssetData& A : All) {
            FString P = A.GetObjectPathString();
            if (P.StartsWith(TEXT("/Script/")) || P.Contains(TEXT("/Saved/"))) { Skipped++; continue; }
            if (!IsRenderableClass(A)) { Skipped++; continue; }
            if (!BPeekMdWanted(P)) { Skipped++; continue; }

            ++Processed;
            // Per-asset pump — load pass spans 5-50% (45-pt band), so
            // each asset advances ~45/TotalWanted of the bar. Verbose
            // log line per asset; dialog text updates in real time.
            if (TotalWanted > 0)
            {
                const float Pct = 5.0f + 45.0f * float(Processed) / float(TotalWanted);
                ReportProgressF(Pct, FString::Printf(
                    TEXT("BPeek: loading %d/%d — %s"),
                    Processed, TotalWanted, *FPaths::GetBaseFilename(P)));
            }

            UObject* Obj = A.GetAsset();
            if (Obj) { Loaded++; }
            else { LoadErrors++; UE_LOG(LogBPeek, Warning, TEXT("[BPeekScan] Failed to load asset: %s"), *P); }
        }
        UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Loaded %d assets (filter-gated), skipped %d, %d load errors"), Loaded, Skipped, LoadErrors);
    }
    ReportProgress(50, TEXT("BPeek: building cross-reference map..."));

    // Now that AssetRegistry finished loading, decide which classes count
    // as "renderable" (emit MD + participate in cross-refs). Used by both
    // the known-path set below and the referencer map further down.
    static const TSet<FTopLevelAssetPath> RenderableBases = {
        FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint")),
        FTopLevelAssetPath(TEXT("/Script/UMG"), TEXT("WidgetBlueprint")),
        FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("AnimBlueprint")),
        FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("UserDefinedEnum")),
        FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("UserDefinedStruct")),
        FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("DataTable")),
        FTopLevelAssetPath(TEXT("/Script/Flow"), TEXT("FlowAsset")),
        FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("World")),
        FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("PrimaryDataAsset")),
        FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("DataAsset")),
        FTopLevelAssetPath(TEXT("/Script/LevelSequence"), TEXT("LevelSequence")),
    };
    static const TSet<FName> RenderableLeafNames = {
        FName(TEXT("Blueprint")), FName(TEXT("WidgetBlueprint")),
        FName(TEXT("AnimBlueprint")), FName(TEXT("UserDefinedEnum")),
        FName(TEXT("UserDefinedStruct")), FName(TEXT("DataTable")),
        FName(TEXT("FlowAsset")), FName(TEXT("World")),
        FName(TEXT("PrimaryDataAsset")), FName(TEXT("DataAsset")),
        FName(TEXT("LevelSequence")),
    };
    auto IsRenderable = [&](const FAssetData& A) -> bool
    {
        if (RenderableLeafNames.Contains(A.AssetClassPath.GetAssetName())) return true;
        TArray<FTopLevelAssetPath> Ancestors;
        ARM.Get().GetAncestorClassNames(A.AssetClassPath, Ancestors);
        for (const FTopLevelAssetPath& P : Ancestors)
            if (RenderableBases.Contains(P)) return true;
        return false;
    };

    // Known-path set: normalized asset paths we'll emit MD for. Used by
    // FBPeekUsedBy to decide link vs name-only for referencers — matches
    // "Known" set: every asset the scanner plans to render, for cross-
    // ref resolution elsewhere. Ancestor lookup picks up project
    // subclasses (GameFeatureData, PrimaryAssetLabel, …).
    if (!BPeekMdDir.IsEmpty()) {
        for (const FAssetData& A : All) {
            if (!IsRenderable(A)) continue;
            FString P = A.GetObjectPathString();
            if (P.StartsWith(TEXT("/Script/"))) continue;
            if (!BPeekMdWanted(P)) continue;
            FString Norm = P;
            int32 Dot;
            if (Norm.FindLastChar(TEXT('.'), Dot)) Norm = Norm.Left(Dot);
            BPeekKnownNormalized.Add(Norm);
        }
        UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] pilot known set: %d assets"), BPeekKnownNormalized.Num());
    }

    auto Root = MakeShared<FJsonObject>();

    // Blueprints
    UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Collecting Blueprints..."));
    TArray<TSharedPtr<FJsonValue>> BPs;
    for (TObjectIterator<UBlueprint> It; It; ++It) {
        UBlueprint* BP = *It;
        FString BPP = BP->GetPathName();
        if (BPP.StartsWith(TEXT("/Script/"))) continue;
        auto O = MakeShared<FJsonObject>();
        O->SetStringField(TEXT("asset_path"), BPP);
        if (BP->ParentClass) O->SetStringField(TEXT("parent_class"), BP->ParentClass->GetName());
        if (BP->GeneratedClass) O->SetStringField(TEXT("generated_class"), BP->GeneratedClass->GetName());
        TArray<TSharedPtr<FJsonValue>> Vars;
        for (const FBPVariableDescription& V : BP->NewVariables) {
            auto VO = MakeShared<FJsonObject>();
            VO->SetStringField(TEXT("name"), V.VarName.ToString());
            VO->SetStringField(TEXT("type"), PinTypeToStr(V.VarType));
            if (!V.Category.IsEmpty()) VO->SetStringField(TEXT("category"), V.Category.ToString());
            if (!V.DefaultValue.IsEmpty()) VO->SetStringField(TEXT("default"), V.DefaultValue);
            if (V.PropertyFlags & CPF_Edit) VO->SetBoolField(TEXT("editable"), true);
            if (V.PropertyFlags & CPF_BlueprintReadOnly) VO->SetBoolField(TEXT("readonly"), true);
            // Tooltip: look it up in FBPVariableDescription.MetaDataArray.
            for (const FBPVariableMetaDataEntry& MD : V.MetaDataArray) {
                if (MD.DataKey == FName(TEXT("tooltip")) && !MD.DataValue.IsEmpty()) {
                    VO->SetStringField(TEXT("tooltip"), MD.DataValue);
                    break;
                }
            }
            Vars.Add(MakeShared<FJsonValueObject>(VO));
        }
        O->SetArrayField(TEXT("variables"), Vars);
        TArray<TSharedPtr<FJsonValue>> Fns;
        for (UEdGraph* G : BP->FunctionGraphs)
            if (G) Fns.Add(MakeShared<FJsonValueString>(G->GetName()));
        O->SetArrayField(TEXT("functions"), Fns);
        if (BP->SimpleConstructionScript) {
            TArray<TSharedPtr<FJsonValue>> Cs;
            for (USCS_Node* N : BP->SimpleConstructionScript->GetAllNodes()) {
                if (!N) continue;
                auto CO = MakeShared<FJsonObject>();
                CO->SetStringField(TEXT("name"), N->GetVariableName().ToString());
                if (N->ComponentClass) CO->SetStringField(TEXT("class"), N->ComponentClass->GetName());
                if (N->ParentComponentOrVariableName != NAME_None) CO->SetStringField(TEXT("parent"), N->ParentComponentOrVariableName.ToString());
                Cs.Add(MakeShared<FJsonValueObject>(CO));
            }
            O->SetArrayField(TEXT("components"), Cs);
        }

        // Events on UbergraphPages. Classic UK2Node_Event covers BeginPlay /
        // Tick / Possessed / InputAxis / component-bound events.
        // UK2Node_CustomEvent covers BP-defined custom events. Enhanced
        // Input + any other K2Node-derived entry (doesn't inherit
        // UK2Node_Event) — match by class name prefix and read the node
        // title as the event label. Order = order of occurrence on
        // the page, which matches the ubergraph section order the
        // compiler emits.
        TArray<TSharedPtr<FJsonValue>> StdEvents;
        TArray<TSharedPtr<FJsonValue>> CustomEvents;
        for (UEdGraph* G : BP->UbergraphPages) {
            if (!G) continue;
            for (UEdGraphNode* N : G->Nodes) {
                if (!N) continue;
                if (UK2Node_CustomEvent* CE = Cast<UK2Node_CustomEvent>(N)) {
                    FString CustomName = CE->CustomFunctionName.ToString();
                    if (!CustomName.IsEmpty())
                        CustomEvents.Add(MakeShared<FJsonValueString>(CustomName));
                    continue;
                }
                if (UK2Node_Event* E = Cast<UK2Node_Event>(N)) {
                    FName EvName = E->EventReference.GetMemberName();
                    if (EvName == NAME_None && !E->CustomFunctionName.IsNone())
                        EvName = E->CustomFunctionName;
                    if (EvName != NAME_None)
                        StdEvents.Add(MakeShared<FJsonValueString>(EvName.ToString()));
                    continue;
                }
                // Non-UK2Node_Event entry point nodes: EnhancedInputAction, etc.
                FString NClass = N->GetClass()->GetName();
                const bool bIsEntry =
                    NClass.Contains(TEXT("Input")) ||
                    NClass.EndsWith(TEXT("Event")) ||
                    NClass.Contains(TEXT("BoundEvent"));
                if (!bIsEntry) continue;
                FString Label;
#if WITH_EDITOR
                Label = N->GetNodeTitle(ENodeTitleType::MenuTitle).ToString();
#else
                Label = NClass;
#endif
                Label.ReplaceInline(TEXT("\n"), TEXT(" "));
                Label.ReplaceInline(TEXT("\r"), TEXT(" "));
                if (!Label.IsEmpty())
                    StdEvents.Add(MakeShared<FJsonValueString>(Label));
            }
        }
        if (StdEvents.Num() > 0) O->SetArrayField(TEXT("events"), StdEvents);
        if (CustomEvents.Num() > 0) O->SetArrayField(TEXT("custom_events"), CustomEvents);

        // GameplayTag literals in any pin across any graph in this BP. Covers
        // K2Node_CallFunction input pins with FGameplayTag defaults (e.g.
        // BroadcastMessage(TAG)), pin literals in Widget BP logic, etc.
        {
            TArray<TSharedPtr<FJsonValue>> BPTagRefs;
            TSet<FString> BPSeenTags;
            auto ScanBPGraph = [&BPSeenTags, &BPTagRefs](UEdGraph* G)
            {
                if (!G) return;
                for (UEdGraphNode* N : G->Nodes) {
                    if (!N) continue;
                    for (UEdGraphPin* Pin : N->Pins) {
                        if (!Pin || Pin->DefaultValue.IsEmpty()) continue;
                        int32 Start = 0;
                        while ((Start = Pin->DefaultValue.Find(TEXT("TagName=\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start)) != INDEX_NONE) {
                            int32 NameFrom = Start + 9;
                            int32 End = Pin->DefaultValue.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, NameFrom);
                            if (End <= NameFrom) break;
                            FString Tag = Pin->DefaultValue.Mid(NameFrom, End - NameFrom);
                            if (!Tag.IsEmpty() && !BPSeenTags.Contains(Tag)) {
                                BPSeenTags.Add(Tag);
                                BPTagRefs.Add(MakeShared<FJsonValueString>(Tag));
                            }
                            Start = End + 1;
                        }
                    }
                }
            };
            for (UEdGraph* G : BP->FunctionGraphs) ScanBPGraph(G);
            for (UEdGraph* G : BP->UbergraphPages) ScanBPGraph(G);
            // Also scan variable defaults as a fallback (rare but cheap).
            for (const FBPVariableDescription& V : BP->NewVariables) {
                if (V.DefaultValue.IsEmpty()) continue;
                int32 Start = 0;
                while ((Start = V.DefaultValue.Find(TEXT("TagName=\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start)) != INDEX_NONE) {
                    int32 NameFrom = Start + 9;
                    int32 End = V.DefaultValue.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, NameFrom);
                    if (End <= NameFrom) break;
                    FString Tag = V.DefaultValue.Mid(NameFrom, End - NameFrom);
                    if (!Tag.IsEmpty() && !BPSeenTags.Contains(Tag)) {
                        BPSeenTags.Add(Tag);
                        BPTagRefs.Add(MakeShared<FJsonValueString>(Tag));
                    }
                    Start = End + 1;
                }
            }
            if (BPTagRefs.Num() > 0) O->SetArrayField(TEXT("tag_refs"), BPTagRefs);
        }

        BPs.Add(MakeShared<FJsonValueObject>(O));
    }
    Root->SetArrayField(TEXT("blueprints"), BPs);
    UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Found %d Blueprints"), BPs.Num());
    // MD writes moved to centralized section after RefCollector is built.

    // DataTables
    UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Collecting DataTables..."));
    TArray<TSharedPtr<FJsonValue>> DTs;
    for (TObjectIterator<UDataTable> It; It; ++It) {
        UDataTable* DT = *It;
        FString DP = DT->GetPathName();
        if (DP.StartsWith(TEXT("/Script/"))) continue;
        auto O = MakeShared<FJsonObject>();
        O->SetStringField(TEXT("asset_path"), DP);
        if (DT->RowStruct) O->SetStringField(TEXT("row_struct"), DT->RowStruct->GetName());
        TArray<TSharedPtr<FJsonValue>> Rows;
        for (const FName& RN : DT->GetRowNames()) {
            auto RO = MakeShared<FJsonObject>();
            RO->SetStringField(TEXT("name"), RN.ToString());
            uint8* RD = DT->FindRowUnchecked(RN);
            if (RD && DT->RowStruct) {
                auto VO = MakeShared<FJsonObject>();
                for (TFieldIterator<FProperty> PIt(DT->RowStruct); PIt; ++PIt) {
                    FString Val; (*PIt)->ExportTextItem_Direct(Val, RD + (*PIt)->GetOffset_ForInternal(), nullptr, nullptr, PPF_None);
                    VO->SetStringField((*PIt)->GetName(), Val);
                }
                RO->SetObjectField(TEXT("values"), VO);
            }
            Rows.Add(MakeShared<FJsonValueObject>(RO));
        }
        O->SetArrayField(TEXT("rows"), Rows);
        DTs.Add(MakeShared<FJsonValueObject>(O));
    }
    Root->SetArrayField(TEXT("data_tables"), DTs);
    UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Found %d DataTables"), DTs.Num());

    // Structs
    UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Collecting UserDefinedStructs..."));
    TArray<TSharedPtr<FJsonValue>> Ss;
    for (TObjectIterator<UUserDefinedStruct> It; It; ++It) {
        UUserDefinedStruct* S = *It;
        FString SP = S->GetPathName();
        if (SP.StartsWith(TEXT("/Script/"))) continue;
        auto O = MakeShared<FJsonObject>();
        O->SetStringField(TEXT("asset_path"), SP);
        O->SetStringField(TEXT("name"), S->GetName());
        TArray<TSharedPtr<FJsonValue>> Fs;
        for (TFieldIterator<FProperty> PIt(S); PIt; ++PIt) {
            auto FO = MakeShared<FJsonObject>();
            FO->SetStringField(TEXT("name"), (*PIt)->GetAuthoredName());
            FO->SetStringField(TEXT("type"), (*PIt)->GetCPPType());
            Fs.Add(MakeShared<FJsonValueObject>(FO));
        }
        O->SetArrayField(TEXT("fields"), Fs);
        Ss.Add(MakeShared<FJsonValueObject>(O));
    }
    Root->SetArrayField(TEXT("structs"), Ss);
    UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Found %d UserDefinedStructs"), Ss.Num());

    // Enums
    UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Collecting UserDefinedEnums..."));
    TArray<TSharedPtr<FJsonValue>> Es;
    for (TObjectIterator<UUserDefinedEnum> It; It; ++It) {
        UUserDefinedEnum* E = *It;
        FString EP = E->GetPathName();
        if (EP.StartsWith(TEXT("/Script/"))) continue;
        auto O = MakeShared<FJsonObject>();
        O->SetStringField(TEXT("asset_path"), EP);
        O->SetStringField(TEXT("name"), E->GetName());
        TArray<TSharedPtr<FJsonValue>> Vs;
        int32 C = E->NumEnums();
        if (C > 0 && E->GetNameStringByIndex(C-1).EndsWith(TEXT("_MAX"))) C--;
        for (int32 i = 0; i < C; i++) {
            auto VO = MakeShared<FJsonObject>();
            FString CN = E->GetNameStringByIndex(i); int32 CI;
            if (CN.FindChar(TEXT(':'), CI)) CN = CN.Mid(CI+2);
            VO->SetStringField(TEXT("name"), CN);
            VO->SetStringField(TEXT("display"), E->GetDisplayNameTextByIndex(i).ToString());
            Vs.Add(MakeShared<FJsonValueObject>(VO));
        }
        O->SetArrayField(TEXT("values"), Vs);
        Es.Add(MakeShared<FJsonValueObject>(O));
    }
    Root->SetArrayField(TEXT("enums"), Es);
    UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Found %d UserDefinedEnums"), Es.Num());

    // Widget Blueprints
    UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Collecting WidgetBlueprints..."));
    TArray<TSharedPtr<FJsonValue>> WBPs;
    for (TObjectIterator<UWidgetBlueprint> It; It; ++It) {
        UWidgetBlueprint* WBP = *It;
        FString WP = WBP->GetPathName();
        if (WP.StartsWith(TEXT("/Script/"))) continue;
        auto O = MakeShared<FJsonObject>();
        O->SetStringField(TEXT("asset_path"), WP);
        if (WBP->WidgetTree && WBP->WidgetTree->RootWidget) {
            auto T = DumpWidget(WBP->WidgetTree->RootWidget);
            if (T) O->SetObjectField(TEXT("widget_tree"), T);
        }
        if (WBP->Animations.Num() > 0) {
            TArray<TSharedPtr<FJsonValue>> Anims;
            for (UWidgetAnimation* Anim : WBP->Animations) {
                if (Anim) Anims.Add(MakeShared<FJsonValueString>(Anim->GetName()));
            }
            if (Anims.Num() > 0) O->SetArrayField(TEXT("animations"), Anims);
        }
        WBPs.Add(MakeShared<FJsonValueObject>(O));
    }
    Root->SetArrayField(TEXT("widget_blueprints"), WBPs);
    UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Found %d WidgetBlueprints"), WBPs.Num());

        // Levels (contributed by LevelProcessor)
    UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Collecting Levels..."));
    TArray<TSharedPtr<FJsonValue>> LvlArr;
    for (TObjectIterator<UWorld> It; It; ++It) {
        UWorld* W = *It;
        if (!W) continue;
        FString WP = W->GetPathName();
        if (WP.StartsWith(TEXT("/Script/")) || WP.StartsWith(TEXT("/Temp/"))) continue;
        if (!W->PersistentLevel) continue;

        auto LO = MakeShared<FJsonObject>();
        LO->SetStringField(TEXT("asset_path"), WP);

        TArray<TSharedPtr<FJsonValue>> Actors;
        for (AActor* A : W->PersistentLevel->Actors) {
            if (!A) continue;
            auto AO = MakeShared<FJsonObject>();
            AO->SetStringField(TEXT("name"), A->GetName());
            AO->SetStringField(TEXT("class"), A->GetClass()->GetPathName());
            const FTransform T = A->GetActorTransform();
            AO->SetStringField(TEXT("location"), T.GetLocation().ToString());
            AO->SetStringField(TEXT("rotation"), T.GetRotation().Rotator().ToString());
            AO->SetStringField(TEXT("scale"), T.GetScale3D().ToString());

            // Properties: full dump for project BPs, skip for engine StaticMeshActor-like decor
            // to keep MD readable. Identify "decor" as actors from /Script/Engine whose class
            // is AStaticMeshActor (or its subclass).
            const bool bIsEngineStaticDecor =
                A->GetClass()->GetPathName().StartsWith(TEXT("/Script/Engine.")) &&
                A->IsA(AStaticMeshActor::StaticClass());

            if (!bIsEngineStaticDecor) {
                auto Props = MakeShared<FJsonObject>();
                for (TFieldIterator<FProperty> PIt(A->GetClass()); PIt; ++PIt) {
                    FProperty* P = *PIt;
                    if (!P->HasAnyPropertyFlags(CPF_Edit)) continue;
                    if (P->HasAnyPropertyFlags(CPF_Transient)) continue;
                    // Skip base-class AActor clutter (Tags, bHidden, etc.) — scenario logic is in subclasses.
                    if (P->GetOwnerClass() == AActor::StaticClass()) continue;
                    const FString PropName = P->GetName();
                    if (PropName.StartsWith(TEXT("bOverride_"))) {
                        if (FBoolProperty* BP = CastField<FBoolProperty>(P);
                            BP && !BP->GetPropertyValue_InContainer(A)) continue;
                    }
                    if (FObjectProperty* OP = CastField<FObjectProperty>(P)) {
                        if (UObject* V = OP->GetObjectPropertyValue_InContainer(A))
                            Props->SetStringField(PropName, V->GetPathName());
                        continue;
                    }
                    if (FSoftObjectProperty* SP = CastField<FSoftObjectProperty>(P)) {
                        const FSoftObjectPtr V = SP->GetPropertyValue_InContainer(A);
                        if (!V.IsNull()) Props->SetStringField(PropName, V.ToString());
                        continue;
                    }
                    if (FTextProperty* TP = CastField<FTextProperty>(P)) {
                        const FText V = TP->GetPropertyValue_InContainer(A);
                        if (!V.IsEmpty()) Props->SetStringField(PropName, V.ToString());
                        continue;
                    }
                    FString Val;
                    P->ExportTextItem_Direct(Val, P->ContainerPtrToValuePtr<void>(A), nullptr, nullptr, PPF_None);
                    if (!Val.IsEmpty() && Val != TEXT("None") && Val != TEXT("\"\"") && Val != TEXT("()"))
                        Props->SetStringField(PropName, Val);
                }
                if (Props->Values.Num() > 0) AO->SetObjectField(TEXT("properties"), Props);
            }
            Actors.Add(MakeShared<FJsonValueObject>(AO));
        }
        LO->SetArrayField(TEXT("actors"), Actors);
        LvlArr.Add(MakeShared<FJsonValueObject>(LO));
    }
    Root->SetArrayField(TEXT("levels"), LvlArr);
    UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Found %d Levels"), LvlArr.Num());

    // Flow metadata collection lives in the BPeekFlow submodule (when
    // the Flow plugin is detected at build time). Core's JSON keeps
    // the "flow_assets" key for schema stability — the array stays
    // empty when BPeekFlow isn't populating it.
    Root->SetArrayField(TEXT("flow_assets"), TArray<TSharedPtr<FJsonValue>>());

    // Level Sequences (contributed by LevelSequenceProcessor)
    UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Collecting LevelSequences..."));
    TArray<TSharedPtr<FJsonValue>> LsArr;
    for (TObjectIterator<ULevelSequence> It; It; ++It) {
        ULevelSequence* LS = *It;
        if (!LS) continue;
        FString LP = LS->GetPathName();
        if (LP.StartsWith(TEXT("/Script/"))) continue;
        UMovieScene* MS = LS->GetMovieScene();
        if (!MS) continue;

        auto O = MakeShared<FJsonObject>();
        O->SetStringField(TEXT("asset_path"), LP);

        const FFrameRate DisplayRate = MS->GetDisplayRate();
        const FFrameRate TickResolution = MS->GetTickResolution();
        const TRange<FFrameNumber> Range = MS->GetPlaybackRange();
        const int32 DurationTicks = Range.Size<FFrameNumber>().Value;
        const int32 DurationDisplay = FFrameRate::TransformTime(
            FFrameTime(FFrameNumber(DurationTicks)), TickResolution, DisplayRate
        ).RoundToFrame().Value;
        O->SetNumberField(TEXT("duration_frames"), DurationDisplay);
        O->SetNumberField(TEXT("frame_rate"), DisplayRate.AsDecimal());

        TArray<TSharedPtr<FJsonValue>> Bindings;
        // Possessables — live actors from the level.
        for (int32 i = 0; i < MS->GetPossessableCount(); i++) {
            const FMovieScenePossessable& Pos = MS->GetPossessable(i);
            auto BO = MakeShared<FJsonObject>();
            BO->SetStringField(TEXT("name"), Pos.GetName());
            BO->SetStringField(TEXT("kind"), TEXT("Possessable"));
            if (UClass* C = const_cast<UClass*>(Pos.GetPossessedObjectClass()))
                BO->SetStringField(TEXT("class"), C->GetPathName());
            TArray<TSharedPtr<FJsonValue>> Tracks;
            for (UMovieSceneTrack* T : MS->FindTracks(UMovieSceneTrack::StaticClass(), Pos.GetGuid(), NAME_None)) {
                if (T) Tracks.Add(MakeShared<FJsonValueString>(T->GetClass()->GetName()));
            }
            BO->SetArrayField(TEXT("tracks"), Tracks);
            Bindings.Add(MakeShared<FJsonValueObject>(BO));
        }
        // Spawnables — objects instantiated by the sequence itself.
        for (int32 i = 0; i < MS->GetSpawnableCount(); i++) {
            const FMovieSceneSpawnable& Sp = MS->GetSpawnable(i);
            auto BO = MakeShared<FJsonObject>();
            BO->SetStringField(TEXT("name"), Sp.GetName());
            BO->SetStringField(TEXT("kind"), TEXT("Spawnable"));
            if (const UObject* Tpl = Sp.GetObjectTemplate())
                BO->SetStringField(TEXT("class"), Tpl->GetClass()->GetPathName());
            TArray<TSharedPtr<FJsonValue>> Tracks;
            for (UMovieSceneTrack* T : MS->FindTracks(UMovieSceneTrack::StaticClass(), Sp.GetGuid(), NAME_None)) {
                if (T) Tracks.Add(MakeShared<FJsonValueString>(T->GetClass()->GetName()));
            }
            BO->SetArrayField(TEXT("tracks"), Tracks);
            Bindings.Add(MakeShared<FJsonValueObject>(BO));
        }
        O->SetArrayField(TEXT("bindings"), Bindings);

        // Director Blueprint — the inner BP that hosts event-track bindings for this
        // sequence. Dump its function names + GameplayTag names found in any
        // FGameplayTag-valued variable default, so cross-refs pick them up.
    #if WITH_EDITOR
        if (UBlueprint* Director = LS->GetDirectorBlueprint()) {
            TArray<TSharedPtr<FJsonValue>> DirFns;
            for (UEdGraph* G : Director->FunctionGraphs)
                if (G) DirFns.Add(MakeShared<FJsonValueString>(G->GetName()));
            for (UEdGraph* G : Director->UbergraphPages) {
                if (!G) continue;
                for (UEdGraphNode* N : G->Nodes) {
                    if (!N) continue;
                    FString NClass = N->GetClass()->GetName();
                    if (NClass == TEXT("K2Node_CustomEvent") || NClass.EndsWith(TEXT("Event")))
                        DirFns.Add(MakeShared<FJsonValueString>(N->GetNodeTitle(ENodeTitleType::MenuTitle).ToString()));
                }
            }
            if (DirFns.Num() > 0) O->SetArrayField(TEXT("director_functions"), DirFns);

            // GameplayTag references — scan pin defaults on every node in every
            // function/ubergraph. Director BPs pass tags as literal inputs to
            // Broadcast/CallFunction nodes (not as variable defaults), so we need
            // to look at node pins, not NewVariables. Dedupe via TSet.
            TArray<TSharedPtr<FJsonValue>> TagRefs;
            TSet<FString> SeenTags;
            auto ScanPinsForTags = [&SeenTags, &TagRefs](UEdGraph* G)
            {
                if (!G) return;
                for (UEdGraphNode* N : G->Nodes) {
                    if (!N) continue;
                    for (UEdGraphPin* Pin : N->Pins) {
                        if (!Pin || Pin->DefaultValue.IsEmpty()) continue;
                        int32 Start = 0;
                        while ((Start = Pin->DefaultValue.Find(TEXT("TagName=\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start)) != INDEX_NONE) {
                            int32 NameFrom = Start + 9;
                            int32 End = Pin->DefaultValue.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, NameFrom);
                            if (End <= NameFrom) break;
                            FString Tag = Pin->DefaultValue.Mid(NameFrom, End - NameFrom);
                            if (!Tag.IsEmpty() && !SeenTags.Contains(Tag)) {
                                SeenTags.Add(Tag);
                                TagRefs.Add(MakeShared<FJsonValueString>(Tag));
                            }
                            Start = End + 1;
                        }
                    }
                }
            };
            for (UEdGraph* G : Director->FunctionGraphs) ScanPinsForTags(G);
            for (UEdGraph* G : Director->UbergraphPages) ScanPinsForTags(G);
            // Also keep variable defaults as a fallback (cheap, sometimes used).
            for (const FBPVariableDescription& V : Director->NewVariables) {
                if (V.DefaultValue.IsEmpty()) continue;
                int32 Start = 0;
                while ((Start = V.DefaultValue.Find(TEXT("TagName=\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start)) != INDEX_NONE) {
                    int32 NameFrom = Start + 9;
                    int32 End = V.DefaultValue.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, NameFrom);
                    if (End <= NameFrom) break;
                    FString Tag = V.DefaultValue.Mid(NameFrom, End - NameFrom);
                    if (!Tag.IsEmpty() && !SeenTags.Contains(Tag)) {
                        SeenTags.Add(Tag);
                        TagRefs.Add(MakeShared<FJsonValueString>(Tag));
                    }
                    Start = End + 1;
                }
            }
            if (TagRefs.Num() > 0) O->SetArrayField(TEXT("director_tag_refs"), TagRefs);
        }
    #endif

        LsArr.Add(MakeShared<FJsonValueObject>(O));
    }
    Root->SetArrayField(TEXT("level_sequences"), LsArr);
    UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Found %d LevelSequences"), LsArr.Num());

    // DataAssets (contributed by DataAssetProcessor)
    UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Collecting DataAssets..."));
    TArray<TSharedPtr<FJsonValue>> DAArr;
    for (TObjectIterator<UDataAsset> It; It; ++It) {
        UDataAsset* DA = *It;
        if (!DA) continue;
        FString DAP = DA->GetPathName();
        if (DAP.StartsWith(TEXT("/Script/"))) continue;
        // Skip CDO — only dump concrete instances.
        if (DA->HasAnyFlags(RF_ClassDefaultObject)) continue;

        auto O = MakeShared<FJsonObject>();
        O->SetStringField(TEXT("asset_path"), DAP);
        O->SetStringField(TEXT("class"), DA->GetClass()->GetName());

        if (UPrimaryDataAsset* PDA = Cast<UPrimaryDataAsset>(DA)) {
            const FPrimaryAssetId PID = PDA->GetPrimaryAssetId();
            if (PID.IsValid()) O->SetStringField(TEXT("primary_asset_type"), PID.PrimaryAssetType.ToString());
        }

        auto Props = MakeShared<FJsonObject>();
        for (TFieldIterator<FProperty> PIt(DA->GetClass()); PIt; ++PIt) {
            FProperty* P = *PIt;
            if (!P->HasAnyPropertyFlags(CPF_Edit)) continue;
            if (P->HasAnyPropertyFlags(CPF_Transient)) continue;
            // Skip base UDataAsset/UPrimaryDataAsset plumbing.
            if (P->GetOwnerClass() == UDataAsset::StaticClass()) continue;
            if (P->GetOwnerClass() == UPrimaryDataAsset::StaticClass()) continue;
            const FString PropName = P->GetName();
            if (PropName.StartsWith(TEXT("bOverride_"))) {
                if (FBoolProperty* BP = CastField<FBoolProperty>(P);
                    BP && !BP->GetPropertyValue_InContainer(DA)) continue;
            }
            if (FObjectProperty* OP = CastField<FObjectProperty>(P)) {
                if (UObject* V = OP->GetObjectPropertyValue_InContainer(DA))
                    Props->SetStringField(PropName, V->GetPathName());
                continue;
            }
            if (FSoftObjectProperty* SP = CastField<FSoftObjectProperty>(P)) {
                const FSoftObjectPtr V = SP->GetPropertyValue_InContainer(DA);
                if (!V.IsNull()) Props->SetStringField(PropName, V.ToString());
                continue;
            }
            if (FTextProperty* TP = CastField<FTextProperty>(P)) {
                const FText V = TP->GetPropertyValue_InContainer(DA);
                if (!V.IsEmpty()) Props->SetStringField(PropName, V.ToString());
                continue;
            }
            FString Val;
            P->ExportTextItem_Direct(Val, P->ContainerPtrToValuePtr<void>(DA), nullptr, nullptr, PPF_None);
            if (!Val.IsEmpty() && Val != TEXT("None") && Val != TEXT("\"\"") && Val != TEXT("()"))
                Props->SetStringField(PropName, Val);
        }
        if (Props->Values.Num() > 0) O->SetObjectField(TEXT("properties"), Props);
        DAArr.Add(MakeShared<FJsonValueObject>(O));
    }
    Root->SetArrayField(TEXT("data_assets"), DAArr);
    UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Found %d DataAssets"), DAArr.Num());

    // Inverse referencers map: for every renderable pilot-scope asset
    // (enum / struct / DT / BP / widget / flow / DA / world / sequence),
    // query AssetRegistry for dependents. Filtered through the pilot
    // whitelist so cross-scope refs don't leak. C# merges this into its
    // ReferencersIndex so "Used by" picks up enum/struct/DT consumers
    // that BP.PackageDependencies doesn't surface.
    //
    // Iterates `All` + IsRenderable directly (instead of the JSON arrays)
    // so contributor-owned types (FlowAsset, DataAsset) are covered too.
    UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Collecting referencers..."));
    TMap<FString, TSet<FString>> RefCollector;
    auto AddRef = [&RefCollector, &BPeekMdFilters](const FString& Target, const FString& Referrer)
    {
        // Canonicalize both sides to the normalized (no `.Name` suffix,
        // no `_C` tail) form so all AR / property-scan / widget-tree
        // sources produce comparable keys. Writers look up by
        // Normalize(AssetPath) — must match what we insert here.
        const FString T = FBPeekAssetPath::Normalize(Target);
        const FString R = FBPeekAssetPath::Normalize(Referrer);
        if (T.Equals(R)) return;
        if (BPeekMdFilters.Num() > 0)
        {
            bool bMatched = false;
            for (const FString& Pref : BPeekMdFilters)
                if (R.StartsWith(Pref)) { bMatched = true; break; }
            if (!bMatched) return;
        }
        RefCollector.FindOrAdd(T).Add(R);
    };

    // Pass 1: AssetRegistry hard dependencies.
    {
        TSet<FString> Processed;
        for (const FAssetData& A : All)
        {
            if (!IsRenderable(A)) continue;
            FString AP = A.GetObjectPathString();
            if (AP.StartsWith(TEXT("/Script/"))) continue;
            if (!BPeekMdWanted(AP)) continue;
            if (Processed.Contains(AP)) continue;
            Processed.Add(AP);

            FString Pkg = AP;
            int32 Dot;
            if (Pkg.FindLastChar(TEXT('.'), Dot)) Pkg = Pkg.Left(Dot);

            TArray<FName> Referencers;
            ARM.Get().GetReferencers(FName(*Pkg), Referencers);
            for (const FName& R : Referencers)
                AddRef(AP, R.ToString());
        }
    }

    // Pass 2: property-text scan. Catches references stored as plain
    // FString / FSoftObjectPath values that the Asset Registry's
    // dependency graph doesn't surface (e.g. a DataAsset field
    // containing "Map:/Game/Maps/L_Level" as a descriptor string).
    auto ScanObjectProps = [&](const FString& Referrer, const TSharedPtr<FJsonObject>& Obj, const TCHAR* PropsField)
    {
        if (!Obj.IsValid()) return;
        const TSharedPtr<FJsonObject>* Props;
        if (!Obj->TryGetObjectField(PropsField, Props)) return;
        for (const auto& KV : (*Props)->Values)
        {
            FString Val;
            if (!KV.Value->TryGetString(Val)) continue;
            for (const FString& Path : FBPeekAssetLinks::ExtractPaths(Val))
                AddRef(Path, Referrer);
        }
    };
    for (const TSharedPtr<FJsonValue>& V : DAArr)
    {
        const TSharedPtr<FJsonObject>* Obj;
        if (!V->TryGetObject(Obj)) continue;
        FString AP;
        if ((*Obj)->TryGetStringField(TEXT("asset_path"), AP))
            ScanObjectProps(AP, *Obj, TEXT("properties"));
    }
    // Flow assets no longer enumerated by core — see the extraction note
    // at the top of the file. A future BPeekFlow-side hook could
    // contribute Flow cross-refs; until then, FlowAsset refs simply
    // don't show up in the referencer map.
    for (const TSharedPtr<FJsonValue>& V : LvlArr)
    {
        const TSharedPtr<FJsonObject>* Obj;
        if (!V->TryGetObject(Obj)) continue;
        FString AP;
        if (!(*Obj)->TryGetStringField(TEXT("asset_path"), AP)) continue;
        const TArray<TSharedPtr<FJsonValue>>* Actors;
        if ((*Obj)->TryGetArrayField(TEXT("actors"), Actors))
            for (const TSharedPtr<FJsonValue>& AV : *Actors)
            {
                const TSharedPtr<FJsonObject>* AObj;
                if (!AV->TryGetObject(AObj)) continue;
                FString Cls;
                if ((*AObj)->TryGetStringField(TEXT("class"), Cls))
                    for (const FString& P : FBPeekAssetLinks::ExtractPaths(Cls))
                        AddRef(P, AP);
                ScanObjectProps(AP, *AObj, TEXT("properties"));
            }
    }
    for (const TSharedPtr<FJsonValue>& V : LsArr)
    {
        const TSharedPtr<FJsonObject>* Obj;
        if (!V->TryGetObject(Obj)) continue;
        FString AP;
        if (!(*Obj)->TryGetStringField(TEXT("asset_path"), AP)) continue;
        const TArray<TSharedPtr<FJsonValue>>* Bindings;
        if ((*Obj)->TryGetArrayField(TEXT("bindings"), Bindings))
            for (const TSharedPtr<FJsonValue>& BV : *Bindings)
            {
                const TSharedPtr<FJsonObject>* BObj;
                if (!BV->TryGetObject(BObj)) continue;
                FString Cls;
                if ((*BObj)->TryGetStringField(TEXT("class"), Cls))
                    for (const FString& P : FBPeekAssetLinks::ExtractPaths(Cls))
                        AddRef(P, AP);
            }
    }
    // Widget Blueprints — walk the widget_tree recursively and pull refs
    // from every node's property values. Mirrors C# EnrichReferencers'
    // BP.WidgetTree walk: style/brush/image paths inside CommonUI widget
    // props (ButtonStyle, TextStyle, …) only surface this way.
    TFunction<void(const TSharedPtr<FJsonObject>&, const FString&)> WalkWidgetNode;
    WalkWidgetNode = [&AddRef, &WalkWidgetNode](const TSharedPtr<FJsonObject>& N, const FString& Owner)
    {
        if (!N.IsValid()) return;
        const TSharedPtr<FJsonObject>* Props;
        if (N->TryGetObjectField(TEXT("properties"), Props))
            for (const auto& KV : (*Props)->Values)
            {
                FString S;
                if (!KV.Value->TryGetString(S)) continue;
                for (const FString& P : FBPeekAssetLinks::ExtractPaths(S))
                    AddRef(P, Owner);
            }
        const TArray<TSharedPtr<FJsonValue>>* Children;
        if (N->TryGetArrayField(TEXT("children"), Children))
            for (const TSharedPtr<FJsonValue>& CV : *Children)
            {
                const TSharedPtr<FJsonObject>* CObj;
                if (CV->TryGetObject(CObj)) WalkWidgetNode(*CObj, Owner);
            }
    };
    for (const TSharedPtr<FJsonValue>& V : WBPs)
    {
        const TSharedPtr<FJsonObject>* Obj;
        if (!V->TryGetObject(Obj)) continue;
        FString AP;
        if (!(*Obj)->TryGetStringField(TEXT("asset_path"), AP)) continue;
        const TSharedPtr<FJsonObject>* Tree;
        if ((*Obj)->TryGetObjectField(TEXT("widget_tree"), Tree))
            WalkWidgetNode(*Tree, AP);
    }

    // Serialize.
    auto RefMap = MakeShared<FJsonObject>();
    for (auto& KV : RefCollector)
    {
        TArray<FString> Kept = KV.Value.Array();
        Kept.Sort([](const FString& A, const FString& B){
            return FBPeekAssetPath::OrdinalIgnoreCaseCompare(A, B) < 0;
        });
        TArray<TSharedPtr<FJsonValue>> JsonKept;
        for (const FString& K : Kept) JsonKept.Add(MakeShared<FJsonValueString>(K));
        RefMap->SetArrayField(KV.Key, JsonKept);
    }
    Root->SetObjectField(TEXT("referencers"), RefMap);
    UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] referencers map: %d assets with dependents"), RefMap->Values.Num());

    // Convert RefCollector → typed TMap the writers expect. Sort each entry
    // with OrdinalIgnoreCase so Used-by lists are deterministic across runs.
    TMap<FString, TArray<FString>> PilotRefs;
    for (auto& KV : RefCollector)
    {
        TArray<FString> Sorted = KV.Value.Array();
        Sorted.Sort([](const FString& A, const FString& B){
            return FBPeekAssetPath::OrdinalIgnoreCaseCompare(A, B) < 0;
        });
        PilotRefs.Add(KV.Key, MoveTemp(Sorted));
    }

    // Project-wide _index.md accumulator — populated by each MD-write
    // loop below, flushed to disk at the end.
    FBPeekIndexBuilder IndexBuilder;

    // TODO-incremental-regen — `-only-changed` flag opts into hash-diff
    // regen: load the previous _bpeek_hashes.json (if any), compare each
    // asset's .uasset MD5 against the stored hash, skip the MD write
    // when hash matches AND the .md is already on disk. Deleted assets
    // (in prior ledger but not this run) get their orphan MDs removed.
    // Default path = unchanged (full rebuild).
    const bool bIncremental = Params.Contains(TEXT("-only-changed"), ESearchCase::IgnoreCase);

    // AI-optimised output is the default. `-verbose` (or the
    // BPEEK_VERBOSE=1 env var fed into Params by deploy-and-run.bat,
    // or the UBPeekSettings toggle) flips back to the expanded
    // human-readable layout (full markdown tables, one-file BP
    // layout, long asset paths).
    const bool bVerboseCli = Params.Contains(TEXT("-verbose"), ESearchCase::IgnoreCase);
    const bool bVerboseMode = bVerboseCli
        || (GetDefault<UBPeekSettings>() && GetDefault<UBPeekSettings>()->bVerboseMode);
    if (bVerboseMode)
    {
        UE_LOG(LogBPeek, Log,
            TEXT("[BPeekScan] verbose mode ENABLED (%s) — falling back to expanded layout"),
            bVerboseCli ? TEXT("-verbose flag") : TEXT("settings"));
    }
    else
    {
        UE_LOG(LogBPeek, Log,
            TEXT("[BPeekScan] default AI-optimised output"));
    }

    FBPeekHashStore HashStore;
    if (bIncremental && !BPeekMdDir.IsEmpty())
    {
        HashStore.Load(BPeekMdDir);
        UE_LOG(LogBPeek, Display,
            TEXT("[BPeekScan] -only-changed: %d prior hashes loaded from %s"),
            HashStore.NumPrior(), *BPeekMdDir);
    }
    // Full-rebuild mode: wipe output contents in-place. Keep the folder
    // itself on disk so file watchers / open editors / IDEs pointed at
    // it don't get surprised by a disappearing-and-reappearing directory.
    // Per-asset mode (BPeekAssetExactSet non-empty) DOES NOT wipe —
    // we're adding/refreshing specific MDs and must leave siblings alone.
    else if (!BPeekMdDir.IsEmpty() && BPeekAssetExactSet.Num() == 0 &&
             FPaths::DirectoryExists(BPeekMdDir))
    {
        IFileManager& FM = IFileManager::Get();
        const FString Glob = BPeekMdDir / TEXT("*");

        TArray<FString> TopFiles;
        FM.FindFiles(TopFiles, *Glob, /*Files*/true, /*Directories*/false);
        for (const FString& F : TopFiles)
            FM.Delete(*(BPeekMdDir / F), /*RequireExists*/false,
                      /*EvenReadOnly*/true, /*Quiet*/true);

        TArray<FString> TopDirs;
        FM.FindFiles(TopDirs, *Glob, /*Files*/false, /*Directories*/true);
        for (const FString& D : TopDirs)
            FM.DeleteDirectory(*(BPeekMdDir / D), /*RequireExists*/false, /*Tree*/true);

        UE_LOG(LogBPeek, Display,
            TEXT("[BPeekScan] Full rebuild: cleared %d file(s) and %d subdir(s) in %s"),
            TopFiles.Num(), TopDirs.Num(), *BPeekMdDir);
    }

    // Decide per-asset whether to regen. Always call VisitAsset so the
    // store is up-to-date for the next run, even in full-rebuild mode.
    auto ShouldRegenMd = [&HashStore, bIncremental](UObject* Asset, const FString& MdPath) -> bool
    {
        const bool bChanged = HashStore.VisitAsset(Asset);
        if (!bIncremental) return true;                      // full mode → regen every asset
        if (bChanged) return true;                           // hash changed
        if (!FPaths::FileExists(MdPath)) return true;        // MD file missing
        return false;                                        // hit: skip regen
    };

    // Incremental-mode skip counters — surfaced in the final log line.
    int32 TotalSkippedMd = 0;
    // ================================================================
    // MD writes — unified IBPeekExtension registry dispatch.
    // ================================================================
    // Every registered extension participates in a single pass.
    //
    // Resolution per asset:
    //   1. Walk each root UClass from the extension registry (dedup'd
    //      so UBlueprint is only iterated once even if Widget/Anim
    //      extensions subscribed to subclasses).
    //   2. ForEachObjectOfClass finds live instances.
    //   3. Priority-sorted FindFor picks the winning extension. When
    //      multiple match (e.g. generic DataAsset fallback vs specific
    //      GAS extension), highest priority wins.
    //   4. The extension's CanHandle confirms, Write renders markdown,
    //      and AppendToIndex registers the _index.md row.
    //
    // The output must stay byte-identical for the same project state
    // until we add new extensions — verified against
    // W:/My/BPeek-golden/v1.0.0-stage-4.1/.
    if (!BPeekMdDir.IsEmpty())
    {
        ReportProgress(50, TEXT("BPeek: writing markdown via extension registry..."));

        // Per-BP stats accumulator wired into the Blueprint extension
        // through ScanContext::CoverageOut.
        FBPeekCoverageStats ProjectCoverage;

        // Shared context for the whole dispatch pass. AssetPath gets
        // updated per-asset below; the other fields are constants.
        FBPeekScanContext Ctx;
        Ctx.Refs = &PilotRefs;
        Ctx.Known = &BPeekKnownNormalized;
        Ctx.IndexBuilder = &IndexBuilder;
        Ctx.CoverageOut = &ProjectCoverage;
        Ctx.bVerboseMode = bVerboseMode;

        // Compute the minimal set of classes to iterate. Union every
        // extension's GetHandledClasses, then drop any class whose
        // parent is also in the set (iterating the parent already hits
        // all descendants via ForEachObjectOfClass).
        const TArray<IBPeekExtension*> Exts = FBPeekExtensionRegistry::GetAll();
        TSet<UClass*> RawClasses;
        for (IBPeekExtension* E : Exts)
            for (UClass* C : E->GetHandledClasses())
                if (C) RawClasses.Add(C);
        TArray<UClass*> RootClasses;
        for (UClass* A : RawClasses)
        {
            bool bHasAncestorInSet = false;
            for (UClass* B : RawClasses)
            {
                if (A != B && A->IsChildOf(B)) { bHasAncestorInSet = true; break; }
            }
            if (!bHasAncestorInSet) RootClasses.Add(A);
        }
        // Stable order for reproducible progress-band layout across
        // runs — ForEachObjectOfClass doesn't re-shuffle, but the set
        // iteration order is nondeterministic.
        RootClasses.Sort([](const UClass& L, const UClass& R)
        {
            return L.GetName() < R.GetName();
        });

        int32 WrittenTotal = 0;
        int32 ClassIdx = 0;
        const int32 ClassCount = FMath::Max(1, RootClasses.Num());
        for (UClass* Cls : RootClasses)
        {
            const float BandLo = 50.0f + 40.0f * ClassIdx / ClassCount;
            ReportProgressF(BandLo,
                FString::Printf(TEXT("BPeek: markdown — %s"), *Cls->GetName()));

            // Snapshot the candidate list FIRST, then iterate without the
            // UObject-hash lock held. Writers legitimately call into code
            // paths that can spawn UObjects during property inspection —
            // the most notable offender being FBPeekIssuesWriter →
            // UEditorValidatorSubsystem::ValidateLoadedAsset, which can
            // lazy-load referenced assets. Mutating the hash while
            // ForEachObjectOfClass holds it trips the engine's check in
            // UObjectHash.cpp and crashes the editor.
            TArray<UObject*> Candidates;
            ForEachObjectOfClass(Cls, [&Candidates](UObject* Asset)
            {
                if (Asset) Candidates.Add(Asset);
            });

            for (UObject* Asset : Candidates)
            {
                if (!Asset || !IsValid(Asset)) continue;
                const FString P = Asset->GetPathName();
                if (P.StartsWith(TEXT("/Script/"))) continue;
                if (!BPeekMdWanted(P)) continue;

                IBPeekExtension* Ext = FBPeekExtensionRegistry::FindFor(Asset);
                if (!Ext) continue;

                const FString MdPath = BPeekMdDir / BPeekAssetPathToMdSubpath(P);
                Ctx.AssetPath = P;
                Ctx.UassetRel = FString();
                Ctx.MdPath    = MdPath;
                Ctx.bIsCooked = false;

                if (!ShouldRegenMd(Asset, MdPath))
                {
                    Ext->AppendToIndex(IndexBuilder, Asset);
                    ++TotalSkippedMd;
                    UE_LOG(LogBPeek, Verbose,
                        TEXT("[dispatch] skip (unchanged) %s → %s"),
                        *Ext->GetId().ToString(), *P);
                    continue;
                }
                FBPeekMarkdownWriter MdW;
                UE_LOG(LogBPeek, Verbose,
                    TEXT("[dispatch] write %s → %s"),
                    *Ext->GetId().ToString(), *P);
                Ext->Write(MdW, Asset, Ctx);
                if (MdW.SaveTo(MdPath))
                {
                    Ext->AppendToIndex(IndexBuilder, Asset);
                    ++WrittenTotal;
                }
                else
                {
                    UE_LOG(LogBPeek, Warning,
                        TEXT("[dispatch] SaveTo failed: %s (ext=%s)"),
                        *MdPath, *Ext->GetId().ToString());
                }
            }
            ++ClassIdx;
        }
        ReportProgress(90, TEXT("BPeek: markdown writes complete"));
        UE_LOG(LogBPeek, Display,
            TEXT("[BPeekScan] MD dispatch: written=%d skipped=%d across %d extension(s)"),
            WrittenTotal, TotalSkippedMd, Exts.Num());

        // --- Coverage report -----------------------------------------
        // Emits _bpeek_coverage.txt next to the markdown output.
        // Surfaces how much of each project's Blueprint graph actually
        // made it into the rendered markdown — useful for measuring
        // how much of the asset's authored logic ends up visible to an
        // AI agent reading the output.
        {
            const int32 Total     = ProjectCoverage.TotalGraphNodes;
            const int32 Comment   = ProjectCoverage.CommentNodes;
            const int32 Reach     = ProjectCoverage.ReachableExecNodes;
            const int32 Orphan    = ProjectCoverage.OrphanExecNodes;
            const int32 Data      = ProjectCoverage.PureDataNodes;
            const int32 LocalVars = ProjectCoverage.LocalVariablesTotal;
            const int32 AnimBPs   = ProjectCoverage.AnimBlueprints;
            const int32 SMs       = ProjectCoverage.AnimStateMachines;
            const int32 AnimStates  = ProjectCoverage.AnimStates;
            const int32 AnimTrans   = ProjectCoverage.AnimTransitions;

            const int32 ExecTotal = Reach + Orphan;
            const float ExecPct = ExecTotal > 0 ? (100.0f * Reach / ExecTotal) : 100.0f;
            const int32 NonComment = FMath::Max(0, Total - Comment);
            const float MdUpperPct = NonComment > 0 ? (100.0f * (Reach + Data) / NonComment) : 0.0f;

            FString Report;
            Report += TEXT("BPeek coverage report\n");
            Report += TEXT("=====================\n\n");
            Report += FString::Printf(TEXT("Blueprints scanned:      %d\n"), ProjectCoverage.Blueprints);
            Report += FString::Printf(TEXT("Total graph nodes:       %d\n"), Total);
            Report += FString::Printf(TEXT("  Exec reachable:        %d   [emitted in ## Logic]\n"), Reach);
            Report += FString::Printf(TEXT("  Pure data (no exec):   %d   [mentioned via (from X) in input pins]\n"), Data);
            Report += FString::Printf(TEXT("  Orphan exec nodes:     %d   [emitted in ## Orphan nodes section, grouped by graph]\n"), Orphan);
            Report += FString::Printf(TEXT("  Comment boxes:         %d   [emitted as _(in \"...\")_ suffix on contained nodes]\n"), Comment);
            Report += FString::Printf(TEXT("Local variables total:   %d   [emitted as **Locals:** block per function entry]\n"), LocalVars);
            Report += TEXT("\n");
            Report += FString::Printf(TEXT("AnimBlueprints:          %d   [emitted as ## State Machines section]\n"), AnimBPs);
            Report += FString::Printf(TEXT("  State machines:        %d\n"), SMs);
            Report += FString::Printf(TEXT("  States:                %d\n"), AnimStates);
            Report += FString::Printf(TEXT("  Transitions:           %d\n"), AnimTrans);
            Report += TEXT("\n");
            Report += FString::Printf(TEXT("Exec coverage (reach / (reach+orphan)):           %.1f%%\n"), ExecPct);
            Report += FString::Printf(TEXT("MD coverage upper bound ((reach+data) / non-comment): %.1f%%\n"), MdUpperPct);
            Report += TEXT("\n");
            Report += TEXT("Notes\n-----\n");
            Report += TEXT("- 'Reach' = unique nodes the exec walker traversed across all entries.\n");
            Report += TEXT("- 'Pure data' = nodes without exec pins (VariableGet, math, pure BP functions, etc.).\n");
            Report += TEXT("  Mentioned in MD via '(from NodeTitle { A: ..., B: ... })' shortcuts in input\n");
            Report += TEXT("  pin values of reachable exec nodes. Recursive unpacking goes up to 3 levels\n");
            Report += TEXT("  deep. Upper bound assumes all data nodes are reached via some input chain;\n");
            Report += TEXT("  true coverage is slightly lower.\n");
            Report += TEXT("- 'Orphan exec' = nodes with exec pins that no entry reaches. WIP/dead code\n");
            Report += TEXT("  or unusual entry points the walker doesn't enumerate.\n");

            const FString CovPath = BPeekMdDir / TEXT("_bpeek_coverage.txt");
            if (FFileHelper::SaveStringToFile(Report, *CovPath,
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            {
                UE_LOG(LogBPeek, Display, TEXT("[BPeek] Coverage report: %s"), *CovPath);
            }
            UE_LOG(LogBPeek, Display,
                TEXT("[BPeek] Coverage: %d BPs, %d total nodes; exec %d/%d (%.1f%%), data %d, orphan %d, comment %d, locals %d"),
                ProjectCoverage.Blueprints, Total,
                Reach, ExecTotal, ExecPct, Data, Orphan, Comment, LocalVars);
        }
    }

    ReportProgress(95, TEXT("BPeek: finalising (tags, index, hashes)..."));
    // Project-wide GameplayTags summary.
    if (!BPeekMdDir.IsEmpty())
    {
        TMap<FString, FBPeekGameplayTag> Tags = FBPeekGameplayTagsWriter::LoadFromIni(FPaths::ProjectDir());

        auto Register = [&Tags, &BPeekMdWanted](const FString& Name, const FString& Owner)
        {
            if (Name.IsEmpty()) return;
            // Filter by pilot scope — C# iterates an already-filtered
            // ProjectIndex, so tags used only outside the whitelist never
            // appear on the C# side.
            if (!BPeekMdWanted(Owner)) return;
            FBPeekGameplayTag* Existing = Tags.Find(Name);
            if (!Existing)
            {
                FBPeekGameplayTag T; T.Name = Name; T.Source = TEXT("runtime");
                Existing = &Tags.Add(Name, MoveTemp(T));
            }
            // Dedupe UsedBy by normalized form — same asset can appear as
            // both "/X.X" and "/X" from different collection paths.
            const FString OwnerNorm = FBPeekAssetPath::Normalize(Owner);
            bool bSeen = false;
            for (const FString& U : Existing->UsedBy)
                if (FBPeekAssetPath::Normalize(U).Equals(OwnerNorm))
                { bSeen = true; break; }
            if (!bSeen) Existing->UsedBy.Add(Owner);
        };
        auto ScanJsonProps = [&Register](const FString& Owner, const TSharedPtr<FJsonObject>& Obj, const TCHAR* PropsField)
        {
            if (!Obj.IsValid()) return;
            const TSharedPtr<FJsonObject>* Props;
            if (!Obj->TryGetObjectField(PropsField, Props)) return;
            for (const auto& KV : (*Props)->Values)
            {
                FString V;
                if (!KV.Value->TryGetString(V)) continue;
                for (const FString& Tag : FBPeekGameplayTagsWriter::ExtractTagLiterals(V))
                    Register(Tag, Owner);
            }
        };

        // BPs — pre-extracted tag_refs. Skip sub-object BPs (their asset_path
        // contains ':') — those are LevelScriptBlueprints / DirectorBPs living
        // inside a .umap or .uasset. C# never indexes them as top-level BPs,
        // so counting their tag_refs would inflate `used by` lists with
        // paths that have no matching MD file.
        for (const TSharedPtr<FJsonValue>& V : BPs)
        {
            const TSharedPtr<FJsonObject>* Obj; if (!V->TryGetObject(Obj)) continue;
            FString AP; if (!(*Obj)->TryGetStringField(TEXT("asset_path"), AP)) continue;
            if (AP.Contains(TEXT(":"))) continue;
            const TArray<TSharedPtr<FJsonValue>>* Refs;
            if ((*Obj)->TryGetArrayField(TEXT("tag_refs"), Refs))
                for (const TSharedPtr<FJsonValue>& R : *Refs) { FString T; if (R->TryGetString(T)) Register(T, AP); }
        }
        // DAs — scan properties.
        for (const TSharedPtr<FJsonValue>& V : DAArr)
        {
            const TSharedPtr<FJsonObject>* Obj; if (!V->TryGetObject(Obj)) continue;
            FString AP; if (!(*Obj)->TryGetStringField(TEXT("asset_path"), AP)) continue;
            ScanJsonProps(AP, *Obj, TEXT("properties"));
        }
        // Flow node references are emitted by the BPeekFlow submodule
        // when the Flow plugin is detected at build time; core doesn't
        // walk Flow graphs itself.
        // Levels — scan actor properties.
        for (const TSharedPtr<FJsonValue>& V : LvlArr)
        {
            const TSharedPtr<FJsonObject>* Obj; if (!V->TryGetObject(Obj)) continue;
            FString AP; if (!(*Obj)->TryGetStringField(TEXT("asset_path"), AP)) continue;
            const TArray<TSharedPtr<FJsonValue>>* Actors;
            if ((*Obj)->TryGetArrayField(TEXT("actors"), Actors))
                for (const TSharedPtr<FJsonValue>& AV : *Actors)
                {
                    const TSharedPtr<FJsonObject>* AObj;
                    if (AV->TryGetObject(AObj)) ScanJsonProps(AP, *AObj, TEXT("properties"));
                }
        }
        // LevelSequences — pre-extracted director_tag_refs.
        for (const TSharedPtr<FJsonValue>& V : LsArr)
        {
            const TSharedPtr<FJsonObject>* Obj; if (!V->TryGetObject(Obj)) continue;
            FString AP; if (!(*Obj)->TryGetStringField(TEXT("asset_path"), AP)) continue;
            const TArray<TSharedPtr<FJsonValue>>* Refs;
            if ((*Obj)->TryGetArrayField(TEXT("director_tag_refs"), Refs))
                for (const TSharedPtr<FJsonValue>& R : *Refs) { FString T; if (R->TryGetString(T)) Register(T, AP); }
        }
        // Widget Blueprints — scan widget_tree recursively. CommonUI's
        // FGameplayTagContainer on widgets (e.g. Layer property on
        // UCommonActivatableWidget) serializes as `TagName="…"` inside the
        // widget tree JSON. Mirrors C# GameplayTagsLoader BP.WidgetTree walk.
        TFunction<void(const TSharedPtr<FJsonObject>&, const FString&)> ScanWidgetForTags;
        ScanWidgetForTags = [&Register, &ScanWidgetForTags](const TSharedPtr<FJsonObject>& N, const FString& Owner)
        {
            if (!N.IsValid()) return;
            const TSharedPtr<FJsonObject>* Props;
            if (N->TryGetObjectField(TEXT("properties"), Props))
                for (const auto& KV : (*Props)->Values)
                {
                    FString S;
                    if (!KV.Value->TryGetString(S)) continue;
                    for (const FString& Tag : FBPeekGameplayTagsWriter::ExtractTagLiterals(S))
                        Register(Tag, Owner);
                }
            const TArray<TSharedPtr<FJsonValue>>* Children;
            if (N->TryGetArrayField(TEXT("children"), Children))
                for (const TSharedPtr<FJsonValue>& CV : *Children)
                {
                    const TSharedPtr<FJsonObject>* CObj;
                    if (CV->TryGetObject(CObj)) ScanWidgetForTags(*CObj, Owner);
                }
        };
        for (const TSharedPtr<FJsonValue>& V : WBPs)
        {
            const TSharedPtr<FJsonObject>* Obj; if (!V->TryGetObject(Obj)) continue;
            FString AP; if (!(*Obj)->TryGetStringField(TEXT("asset_path"), AP)) continue;
            const TSharedPtr<FJsonObject>* Tree;
            if ((*Obj)->TryGetObjectField(TEXT("widget_tree"), Tree))
                ScanWidgetForTags(*Tree, AP);
        }

        FBPeekMarkdownWriter GtW;
        FBPeekGameplayTagsWriter::WriteAll(GtW, Tags);
        const FString GtPath = BPeekMdDir / TEXT("GameplayTags.md");
        GtW.SaveTo(GtPath);
        UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Wrote GameplayTags.md: %d tags"), Tags.Num());
    }

    // --- _index.md — project-wide overview for AI-consumption ---------
    // Populated alongside each per-asset write above; emitted last so the
    // summary counts reflect assets actually written (respecting wanted-
    // filter rules). Intent: one file agents Read first to get the map
    // of what's in the project. Always regenerated (even in incremental
    // mode) because it's an aggregate view — any asset delta invalidates
    // it.
    if (!BPeekMdDir.IsEmpty())
    {
        FString ProjectName = FApp::GetProjectName();
        if (IndexBuilder.Write(BPeekMdDir, ProjectName, bVerboseMode))
        {
            UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Wrote _index.md"));
        }
        else
        {
            UE_LOG(LogBPeek, Warning, TEXT("[BPeekScan] Failed to write _index.md"));
        }
        // TODO-ai-consumption Layer 1 #3 — companion summary files.
        // Same data as _index.md, regrouped: one by type (no summary
        // header), one by module (grouped by top two path segments).
        if (IndexBuilder.WriteByType(BPeekMdDir, ProjectName))
        {
            UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Wrote _summary-by-type.md"));
        }
        else
        {
            UE_LOG(LogBPeek, Warning, TEXT("[BPeekScan] Failed to write _summary-by-type.md"));
        }
        if (IndexBuilder.WriteByModule(BPeekMdDir, ProjectName))
        {
            UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Wrote _summary-by-module.md"));
        }
        else
        {
            UE_LOG(LogBPeek, Warning, TEXT("[BPeekScan] Failed to write _summary-by-module.md"));
        }
    }

    // --- Hash ledger + orphan MD cleanup (incremental regen) ----------
    // In incremental mode: remove MDs for assets present in the prior
    // hash ledger but not revisited this run (i.e., deleted since last
    // run). Always save the current hash map so the next `-only-changed`
    // run has a baseline — even a full rebuild benefits from writing the
    // ledger so a subsequent incremental run can skip everything.
    if (!BPeekMdDir.IsEmpty())
    {
        int32 OrphanMdRemoved = 0;
        if (bIncremental)
        {
            for (const FString& DeletedPath : HashStore.GetDeletedAssets())
            {
                const FString OrphanMdPath = BPeekMdDir / BPeekAssetPathToMdSubpath(DeletedPath);
                if (FPaths::FileExists(OrphanMdPath) &&
                    IFileManager::Get().Delete(*OrphanMdPath))
                {
                    ++OrphanMdRemoved;
                }
            }
        }

        HashStore.Save(BPeekMdDir);
        if (bIncremental)
        {
            UE_LOG(LogBPeek, Display,
                TEXT("[BPeekScan] Incremental run: %d hashes persisted, skipped %d unchanged MDs, removed %d orphan MDs"),
                HashStore.NumCurrent(), TotalSkippedMd, OrphanMdRemoved);
        }
        else
        {
            UE_LOG(LogBPeek, Display,
                TEXT("[BPeekScan] Full rebuild: %d hashes persisted for next -only-changed run"),
                HashStore.NumCurrent());
        }
    }

    UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Serializing JSON..."));
    FString Json; auto W = TJsonWriterFactory<>::Create(&Json);
    FJsonSerializer::Serialize(Root, W); W->Close();
    if (FFileHelper::SaveStringToFile(Json, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) {
        UE_LOG(LogBPeek, Display, TEXT("[BPeekScan] Success: %d BPs, %d DTs, %d structs, %d enums, %d widgets -> %s"),
            BPs.Num(), DTs.Num(), Ss.Num(), Es.Num(), WBPs.Num(), *OutputPath);
        ReportProgress(100, TEXT("BPeek: done."));
        return 0;
    }
    UE_LOG(LogBPeek, Error, TEXT("[BPeekScan] Failed: %s"), *OutputPath);
    return 1;
}