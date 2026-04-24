#pragma once
#include "CoreMinimal.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/DataAsset.h"
#include "Engine/UserDefinedEnum.h"
#include "BPeekCompat.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "BPeekAssetPathHelpers.h"

/**
 * Accumulates per-type one-liner entries for the project-wide `_index.md`
 * file. The commandlet's main write loops call AddXxx() at the same point
 * they SaveTo() each per-asset MD, so the index stays in sync with what
 * was actually emitted (no ghost entries for assets excluded by wanted-
 * filter).
 *
 * One file emitted at the end of the run:
 *
 *   # {ProjectName} — BPeek index
 *
 *   ## Summary
 *   - **Blueprints**: 630
 *   - **Widget Blueprints**: 250
 *   - ...
 *
 *   ## Blueprints (630)
 *   - [B_Hero_Default](Game/Characters/Heroes/B_Hero_Default.md) — parent: LyraCharacter · 6 events
 *   - ...
 *
 *   ## Enums (50)
 *   - ...
 *
 * The file lands around ~80-120 KB on a ~1200-asset project — fits
 * in a single AI-agent context window, loads with one read call, and
 * gives the agent a project-wide map to navigate from.
 */
class FBPeekIndexBuilder
{
public:
    struct FEntry
    {
        FString Name;         // "B_Hero_Default"
        FString MdRelPath;    // "Game/Characters/Heroes/B_Hero_Default.md"
        FString OneLiner;     // "parent: LyraCharacter · 6 events"
    };

    TArray<FEntry> Blueprints;
    TArray<FEntry> WidgetBlueprints;
    TArray<FEntry> AnimBlueprints;
    TArray<FEntry> Enums;
    TArray<FEntry> Structs;
    TArray<FEntry> DataTables;
    TArray<FEntry> Flows;
    TArray<FEntry> LevelSequences;
    TArray<FEntry> DataAssets;
    TArray<FEntry> Levels;

    /** Dispatches UBlueprint → the right bucket (Widget/Anim/plain BP). */
    void AddBlueprint(UBlueprint* BP)
    {
        if (!BP) return;
        const FString MdRel = FBPeekAssetPath::ToMdSubpath(BP->GetPathName());

        if (UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(BP))
        {
            FEntry E;
            E.Name = WBP->GetName();
            E.MdRelPath = MdRel;
            E.OneLiner = ParentClassLiner(WBP);
            WidgetBlueprints.Add(MoveTemp(E));
            return;
        }
        if (UAnimBlueprint* ABP = Cast<UAnimBlueprint>(BP))
        {
            FEntry E;
            E.Name = ABP->GetName();
            E.MdRelPath = MdRel;
            E.OneLiner = AnimBlueprintLiner(ABP);
            AnimBlueprints.Add(MoveTemp(E));
            return;
        }

        FEntry E;
        E.Name = BP->GetName();
        E.MdRelPath = MdRel;
        E.OneLiner = BlueprintLiner(BP);
        Blueprints.Add(MoveTemp(E));
    }

    void AddEnum(UEnum* E)
    {
        if (!E) return;
        FEntry X;
        X.Name = E->GetName();
        X.MdRelPath = FBPeekAssetPath::ToMdSubpath(E->GetPathName());
        // NumEnums() includes the auto-added `_MAX` sentinel — display
        // NumEnums()-1 as the intuitive "value count" for BP authors.
        const int32 Count = FMath::Max(0, E->NumEnums() - 1);
        X.OneLiner = FString::Printf(TEXT("%d value%s"), Count, Count == 1 ? TEXT("") : TEXT("s"));
        Enums.Add(MoveTemp(X));
    }

    void AddStruct(UUserDefinedStruct* S)
    {
        if (!S) return;
        FEntry E;
        E.Name = S->GetName();
        E.MdRelPath = FBPeekAssetPath::ToMdSubpath(S->GetPathName());
        int32 Fields = 0;
        for (TFieldIterator<FProperty> It(S); It; ++It) ++Fields;
        E.OneLiner = FString::Printf(TEXT("%d field%s"), Fields, Fields == 1 ? TEXT("") : TEXT("s"));
        Structs.Add(MoveTemp(E));
    }

    void AddDataTable(UDataTable* DT)
    {
        if (!DT) return;
        FEntry E;
        E.Name = DT->GetName();
        E.MdRelPath = FBPeekAssetPath::ToMdSubpath(DT->GetPathName());
        const int32 Rows = DT->GetRowMap().Num();
        const FString StructName = DT->RowStruct ? DT->RowStruct->GetName() : FString(TEXT("?"));
        E.OneLiner = FString::Printf(TEXT("%d row%s · `%s`"), Rows, Rows == 1 ? TEXT("") : TEXT("s"), *StructName);
        DataTables.Add(MoveTemp(E));
    }

    /** Add a Flow row. Caller (the BPeekFlow extension plugin) passes
     *  a ready-made FEntry so core doesn't need to #include FlowAsset.h
     *  or link against the Flow module. Kept as a named method so
     *  future typed extensions can follow the same convention without
     *  exposing the raw Flows bucket. */
    void AddFlowEntry(FEntry E)
    {
        Flows.Add(MoveTemp(E));
    }

    void AddLevelSequence(ULevelSequence* LS)
    {
        if (!LS) return;
        FEntry E;
        E.Name = LS->GetName();
        E.MdRelPath = FBPeekAssetPath::ToMdSubpath(LS->GetPathName());
        int32 TrackCount = 0;
        if (UMovieScene* MS = LS->GetMovieScene())
            TrackCount = MS->GetTracks().Num();
        E.OneLiner = FString::Printf(TEXT("%d track%s"), TrackCount, TrackCount == 1 ? TEXT("") : TEXT("s"));
        LevelSequences.Add(MoveTemp(E));
    }

    void AddDataAsset(UDataAsset* DA)
    {
        if (!DA) return;
        FEntry E;
        E.Name = DA->GetName();
        E.MdRelPath = FBPeekAssetPath::ToMdSubpath(DA->GetPathName());
        E.OneLiner = FString::Printf(TEXT("`%s`"), *DA->GetClass()->GetName());
        DataAssets.Add(MoveTemp(E));
    }

    void AddLevel(UWorld* W)
    {
        if (!W || !W->PersistentLevel) return;
        FEntry E;
        E.Name = W->GetName();
        E.MdRelPath = FBPeekAssetPath::ToMdSubpath(W->GetPathName());
        const int32 Actors = W->PersistentLevel->Actors.Num();
        E.OneLiner = FString::Printf(TEXT("%d actor%s"), Actors, Actors == 1 ? TEXT("") : TEXT("s"));
        Levels.Add(MoveTemp(E));
    }

    /** Emits `_index.md` into OutputDir. Sorts each bucket by Name before
     *  write for stable output across runs. Counts total assets emitted. */
    bool Write(const FString& OutputDir, const FString& ProjectName = FString(),
               bool bVerboseMode = false) const
    {
        const FString FilePath = OutputDir / TEXT("_index.md");
        FString Body;
        Body.Reserve(64 * 1024);

        const FString Title = ProjectName.IsEmpty()
            ? FString(TEXT("BPeek project index"))
            : FString::Printf(TEXT("%s — BPeek index"), *ProjectName);
        Body += FString::Printf(TEXT("# %s\r\n\r\n"), *Title);
        Body += TEXT("*Auto-generated by BPeek. Load this file first to navigate the project.*\r\n\r\n");
        if (!bVerboseMode)
        {
            Body += TEXT("> **AI-optimised output.** Asset paths drop the `/Game/` mount point "
                         "and the duplicate `.Name` suffix (e.g. `/Game/X/BP_Foo.BP_Foo` "
                         "renders as `X/BP_Foo`). `/Script/...` engine/plugin type refs "
                         "are left verbatim. Blueprint `## Logic` sections live in "
                         "companion `.logic.md` files — load on demand.\r\n\r\n");
        }

        // --- Summary header --------------------------------------------
        const int32 Total =
            Blueprints.Num() + WidgetBlueprints.Num() + AnimBlueprints.Num() +
            Enums.Num() + Structs.Num() + DataTables.Num() + Flows.Num() +
            LevelSequences.Num() + DataAssets.Num() + Levels.Num();

        Body += FString::Printf(TEXT("**Total**: %d assets\r\n\r\n"), Total);
        Body += TEXT("## Summary\r\n\r\n");
        auto AddSummary = [&Body](const FString& Label, int32 Count, const FString& Anchor)
        {
            if (Count == 0) return;
            Body += FString::Printf(TEXT("- **%s**: %d ([jump](#%s))\r\n"),
                *Label, Count, *Anchor);
        };
        AddSummary(TEXT("Blueprints"),         Blueprints.Num(),         TEXT("blueprints"));
        AddSummary(TEXT("Widget Blueprints"),  WidgetBlueprints.Num(),   TEXT("widget-blueprints"));
        AddSummary(TEXT("Anim Blueprints"),    AnimBlueprints.Num(),     TEXT("anim-blueprints"));
        AddSummary(TEXT("Enums"),              Enums.Num(),              TEXT("enums"));
        AddSummary(TEXT("Structs"),            Structs.Num(),            TEXT("structs"));
        AddSummary(TEXT("Data Tables"),        DataTables.Num(),         TEXT("data-tables"));
        AddSummary(TEXT("Data Assets"),        DataAssets.Num(),         TEXT("data-assets"));
        AddSummary(TEXT("Level Sequences"),    LevelSequences.Num(),     TEXT("level-sequences"));
        AddSummary(TEXT("Flow Assets"),        Flows.Num(),              TEXT("flow-assets"));
        AddSummary(TEXT("Levels"),             Levels.Num(),             TEXT("levels"));
        Body += TEXT("\r\n");

        // --- Per-type sections -----------------------------------------
        WriteSection(Body, TEXT("Blueprints"),        Blueprints);
        WriteSection(Body, TEXT("Widget Blueprints"), WidgetBlueprints);
        WriteSection(Body, TEXT("Anim Blueprints"),   AnimBlueprints);
        WriteSection(Body, TEXT("Enums"),             Enums);
        WriteSection(Body, TEXT("Structs"),           Structs);
        WriteSection(Body, TEXT("Data Tables"),       DataTables);
        WriteSection(Body, TEXT("Data Assets"),       DataAssets);
        WriteSection(Body, TEXT("Level Sequences"),   LevelSequences);
        WriteSection(Body, TEXT("Flow Assets"),       Flows);
        WriteSection(Body, TEXT("Levels"),            Levels);

        return FFileHelper::SaveStringToFile(
            Body, *FilePath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

    /** Emits `_summary-by-type.md`. Same per-type sections as
     *  `_index.md` but without the Summary block — for agents that
     *  want the raw list without the overview header. */
    bool WriteByType(const FString& OutputDir, const FString& ProjectName = FString()) const
    {
        const FString FilePath = OutputDir / TEXT("_summary-by-type.md");
        FString Body;
        Body.Reserve(64 * 1024);

        const FString Title = ProjectName.IsEmpty()
            ? FString(TEXT("BPeek summary — by type"))
            : FString::Printf(TEXT("%s — BPeek summary by type"), *ProjectName);
        Body += FString::Printf(TEXT("# %s\r\n\r\n"), *Title);
        Body += TEXT("*Per-type asset listings, stable-sorted. Companion to `_index.md`.*\r\n\r\n");

        WriteSection(Body, TEXT("Blueprints"),        Blueprints);
        WriteSection(Body, TEXT("Widget Blueprints"), WidgetBlueprints);
        WriteSection(Body, TEXT("Anim Blueprints"),   AnimBlueprints);
        WriteSection(Body, TEXT("Enums"),             Enums);
        WriteSection(Body, TEXT("Structs"),           Structs);
        WriteSection(Body, TEXT("Data Tables"),       DataTables);
        WriteSection(Body, TEXT("Data Assets"),       DataAssets);
        WriteSection(Body, TEXT("Level Sequences"),   LevelSequences);
        WriteSection(Body, TEXT("Flow Assets"),       Flows);
        WriteSection(Body, TEXT("Levels"),            Levels);

        return FFileHelper::SaveStringToFile(
            Body, *FilePath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

    /** Emits `_summary-by-module.md`. Groups every asset by the top
     *  two path segments of its MdRelPath (e.g. `Game/Characters`,
     *  `ShooterCore/Bot`) — gives the AI a fast "what's in this
     *  module" view without grep. Within each module: inline
     *  one-liners grouped by type. */
    bool WriteByModule(const FString& OutputDir, const FString& ProjectName = FString()) const
    {
        const FString FilePath = OutputDir / TEXT("_summary-by-module.md");
        FString Body;
        Body.Reserve(64 * 1024);

        const FString Title = ProjectName.IsEmpty()
            ? FString(TEXT("BPeek summary — by module"))
            : FString::Printf(TEXT("%s — BPeek summary by module"), *ProjectName);
        Body += FString::Printf(TEXT("# %s\r\n\r\n"), *Title);
        Body += TEXT("*Assets grouped by the top-level module folder (`Game/UI`, `Game/Characters`, `ShooterCore/Bot`, ...).*\r\n\r\n");

        // Type label → entries list within each module. Use TMap-of-TMap
        // with module as outer key.
        TMap<FString, TMap<FString, TArray<FEntry>>> ByModule;
        const TArray<TPair<FString, const TArray<FEntry>*>> Typed = {
            { TEXT("Blueprints"),        &Blueprints },
            { TEXT("Widget Blueprints"), &WidgetBlueprints },
            { TEXT("Anim Blueprints"),   &AnimBlueprints },
            { TEXT("Enums"),             &Enums },
            { TEXT("Structs"),           &Structs },
            { TEXT("Data Tables"),       &DataTables },
            { TEXT("Data Assets"),       &DataAssets },
            { TEXT("Level Sequences"),   &LevelSequences },
            { TEXT("Flow Assets"),       &Flows },
            { TEXT("Levels"),            &Levels },
        };
        for (const auto& TL : Typed)
            for (const FEntry& E : *TL.Value)
                ByModule.FindOrAdd(ExtractModule(E.MdRelPath))
                       .FindOrAdd(TL.Key).Add(E);

        // Deterministic module ordering.
        TArray<FString> ModuleNames;
        ByModule.GetKeys(ModuleNames);
        ModuleNames.Sort([](const FString& A, const FString& B){
            return FBPeekAssetPath::OrdinalIgnoreCaseCompare(A, B) < 0;
        });

        for (const FString& Mod : ModuleNames)
        {
            const TMap<FString, TArray<FEntry>>& Groups = ByModule[Mod];
            int32 ModTotal = 0;
            for (const auto& KV : Groups) ModTotal += KV.Value.Num();
            Body += FString::Printf(TEXT("## %s  *(%d asset%s)*\r\n\r\n"),
                *Mod, ModTotal, ModTotal == 1 ? TEXT("") : TEXT("s"));

            // Emit in the canonical type order so runs are reproducible.
            for (const auto& TL : Typed)
            {
                const TArray<FEntry>* Entries = Groups.Find(TL.Key);
                if (!Entries || Entries->Num() == 0) continue;
                TArray<FEntry> Sorted = *Entries;
                Sorted.Sort([](const FEntry& A, const FEntry& B){
                    const int32 ByName = FBPeekAssetPath::OrdinalIgnoreCaseCompare(A.Name, B.Name);
                    if (ByName != 0) return ByName < 0;
                    return FBPeekAssetPath::OrdinalIgnoreCaseCompare(A.MdRelPath, B.MdRelPath) < 0;
                });
                Body += FString::Printf(TEXT("### %s (%d)\r\n\r\n"), *TL.Key, Sorted.Num());
                for (const FEntry& E : Sorted)
                {
                    if (E.OneLiner.IsEmpty())
                        Body += FString::Printf(TEXT("- [%s](%s)\r\n"), *E.Name, *E.MdRelPath);
                    else
                        Body += FString::Printf(TEXT("- [%s](%s) — %s\r\n"),
                            *E.Name, *E.MdRelPath, *E.OneLiner);
                }
                Body += TEXT("\r\n");
            }
        }

        return FFileHelper::SaveStringToFile(
            Body, *FilePath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

private:
    /** "Game/Characters/Heroes/BP_Foo.md" → "Game/Characters".
     *  For shallow paths ("Game/BP_Bar.md") → "Game" (the second
     *  segment is the asset file itself, not a subdirectory). */
    static FString ExtractModule(const FString& MdRelPath)
    {
        TArray<FString> Segs;
        MdRelPath.ParseIntoArray(Segs, TEXT("/"), /*CullEmpty=*/true);
        if (Segs.Num() == 0) return TEXT("<unknown>");
        if (Segs.Num() == 1) return Segs[0];
        // Last segment is the .md file. If only 2 segments remain, the
        // second one IS the file — grouping key is the first segment
        // alone. With ≥ 3 segments, the first two are real directories.
        if (Segs.Num() == 2) return Segs[0];
        return Segs[0] + TEXT("/") + Segs[1];
    }


    /** Append one `## Heading (N)` + bullet list. Skipped if empty. */
    static void WriteSection(FString& Body, const FString& Title,
                             const TArray<FEntry>& InEntries)
    {
        if (InEntries.Num() == 0) return;
        Body += FString::Printf(TEXT("## %s (%d)\r\n\r\n"), *Title, InEntries.Num());

        TArray<FEntry> Sorted = InEntries;
        // Composite sort: primary by Name, tie-break by MdRelPath. Name
        // alone is not unique (e.g. W_Tool_3 exists in several folders)
        // and relying on the original TObjectIterator order for the tie
        // breaks scan-to-scan reproducibility — the same project rescan
        // shuffles the _index.md rows.
        Sorted.Sort([](const FEntry& A, const FEntry& B){
            const int32 ByName = FBPeekAssetPath::OrdinalIgnoreCaseCompare(A.Name, B.Name);
            if (ByName != 0) return ByName < 0;
            return FBPeekAssetPath::OrdinalIgnoreCaseCompare(A.MdRelPath, B.MdRelPath) < 0;
        });
        for (const FEntry& E : Sorted)
        {
            if (E.OneLiner.IsEmpty())
                Body += FString::Printf(TEXT("- [%s](%s)\r\n"), *E.Name, *E.MdRelPath);
            else
                Body += FString::Printf(TEXT("- [%s](%s) — %s\r\n"),
                    *E.Name, *E.MdRelPath, *E.OneLiner);
        }
        Body += TEXT("\r\n");
    }

    /** "parent: X · N events" for plain BP. */
    static FString BlueprintLiner(UBlueprint* BP)
    {
        TArray<FString> Parts;
        if (BP->ParentClass)
            Parts.Add(FString::Printf(TEXT("parent: `%s`"), *BP->ParentClass->GetName()));

        int32 EventCount = 0;
        for (UEdGraph* G : BP->UbergraphPages)
        {
            if (!G) continue;
            for (UEdGraphNode* N : G->Nodes)
            {
                if (!N) continue;
                const FString Cls = N->GetClass()->GetName();
                if (Cls == TEXT("K2Node_Event") ||
                    Cls == TEXT("K2Node_CustomEvent") ||
                    Cls.Contains(TEXT("Input")))
                {
                    ++EventCount;
                }
            }
        }
        if (EventCount > 0)
            Parts.Add(FString::Printf(TEXT("%d event%s"), EventCount, EventCount == 1 ? TEXT("") : TEXT("s")));

        return FString::Join(Parts, TEXT(" · "));
    }

    /** "parent: X · N widgets" for WidgetBP. */
    static FString ParentClassLiner(UWidgetBlueprint* WBP)
    {
        TArray<FString> Parts;
        if (WBP->ParentClass)
            Parts.Add(FString::Printf(TEXT("parent: `%s`"), *WBP->ParentClass->GetName()));
        if (WBP->WidgetTree)
        {
            int32 WidgetCount = 0;
            WBP->WidgetTree->ForEachWidget([&WidgetCount](UWidget*){ ++WidgetCount; });
            if (WidgetCount > 0)
                Parts.Add(FString::Printf(TEXT("%d widget%s"), WidgetCount, WidgetCount == 1 ? TEXT("") : TEXT("s")));
        }
        return FString::Join(Parts, TEXT(" · "));
    }

    /** "parent: X · N states, M transitions" for AnimBP. */
    static FString AnimBlueprintLiner(UAnimBlueprint* ABP)
    {
        TArray<FString> Parts;
        if (ABP->ParentClass)
            Parts.Add(FString::Printf(TEXT("parent: `%s`"), *ABP->ParentClass->GetName()));

        if (UAnimBlueprintGeneratedClass* GC = Cast<UAnimBlueprintGeneratedClass>(ABP->GeneratedClass))
        {
            int32 Total = 0;
            for (const FBakedAnimationStateMachine& SM : GC->BakedStateMachines)
                Total += SM.States.Num();
            if (Total > 0)
                Parts.Add(FString::Printf(TEXT("%d state%s"), Total, Total == 1 ? TEXT("") : TEXT("s")));
        }
        return FString::Join(Parts, TEXT(" · "));
    }
};
