#pragma once
#include "CoreMinimal.h"
#include "BPeekMarkdownWriter.h"
#include "BPeekAssetLinks.h"
#include "BPeekAssetPathHelpers.h"
#include "BPeekUsedBy.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneTrack.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSpawnable.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"

class FBPeekLevelSequenceWriter
{
public:
    static void Write(FBPeekMarkdownWriter& W, ULevelSequence* LS,
                      const TMap<FString, TArray<FString>>& Refs = TMap<FString, TArray<FString>>(),
                      const TSet<FString>& KnownNormalized = TSet<FString>(),
                      bool bVerboseMode = false)   // Tracks/possessables blocks already 1 line per entry — compact reuses the verbose layout, only the Asset path row is shortened.
    {
        if (!LS) return;
        UMovieScene* MS = LS->GetMovieScene();
        if (!MS) return;

        const FString AssetPath = LS->GetPathName();
        const FString DisplayPath = bVerboseMode ? AssetPath : FBPeekAssetPath::Compact(AssetPath);
        const FString Name = FBPeekAssetPath::ShortName(AssetPath);

        const FFrameRate DisplayRate = MS->GetDisplayRate();
        const FFrameRate TickResolution = MS->GetTickResolution();
        const TRange<FFrameNumber> Range = MS->GetPlaybackRange();
        const int32 DurationTicks = Range.Size<FFrameNumber>().Value;
        const int32 DurationDisplay = FFrameRate::TransformTime(
            FFrameTime(FFrameNumber(DurationTicks)), TickResolution, DisplayRate
        ).RoundToFrame().Value;
        const double FPS = DisplayRate.AsDecimal();
        const double Seconds = FPS > 0 ? DurationDisplay / FPS : 0.0;

        W.WriteHeading(1, FString::Printf(TEXT("%s (Level Sequence)"), *Name));
        W.WriteLine();
        W.WriteMetaRowCode(TEXT("Asset path"), DisplayPath);
        // C# uses InvariantCulture "{0:F2}" — `.` decimal, 2 digits.
        W.WriteLine(FString::Printf(
            TEXT("- **Duration**: %d frames (%.2f sec @ %.2f fps)"),
            DurationDisplay, Seconds, FPS));
        W.WriteLine();

        struct FBindingInfo { FString Name; FString Kind; FString ClassName; TArray<FString> Tracks; };
        TArray<FBindingInfo> Bindings;
        for (int32 i = 0; i < MS->GetPossessableCount(); ++i)
        {
            const FMovieScenePossessable& Pos = MS->GetPossessable(i);
            FBindingInfo B;
            B.Name = Pos.GetName();
            B.Kind = TEXT("Possessable");
            if (UClass* C = const_cast<UClass*>(Pos.GetPossessedObjectClass()))
                B.ClassName = C->GetPathName();
            for (UMovieSceneTrack* T : MS->FindTracks(UMovieSceneTrack::StaticClass(), Pos.GetGuid(), NAME_None))
                if (T) B.Tracks.Add(T->GetClass()->GetName());
            Bindings.Add(MoveTemp(B));
        }
        for (int32 i = 0; i < MS->GetSpawnableCount(); ++i)
        {
            const FMovieSceneSpawnable& Sp = MS->GetSpawnable(i);
            FBindingInfo B;
            B.Name = Sp.GetName();
            B.Kind = TEXT("Spawnable");
            if (const UObject* Tpl = Sp.GetObjectTemplate())
                B.ClassName = Tpl->GetClass()->GetPathName();
            for (UMovieSceneTrack* T : MS->FindTracks(UMovieSceneTrack::StaticClass(), Sp.GetGuid(), NAME_None))
                if (T) B.Tracks.Add(T->GetClass()->GetName());
            Bindings.Add(MoveTemp(B));
        }

        if (Bindings.Num() == 0)
        {
            W.WriteLine(TEXT("_No bindings (empty sequence)._"));
            FBPeekUsedBy::Write(W, Refs, AssetPath, KnownNormalized);
            return;
        }

        W.WriteHeading(2, FString::Printf(TEXT("Bindings (%d)"), Bindings.Num()));
        W.WriteLine();
        W.WriteLine(TEXT("| Name | Kind | Class | Tracks |"));
        W.WriteLine(TEXT("|---|---|---|---|"));
        for (const FBindingInfo& B : Bindings)
        {
            const FString Cls = B.ClassName.IsEmpty()
                ? FString()
                : FBPeekAssetLinks::Linkify(B.ClassName, AssetPath, KnownNormalized);
            const FString Tracks = B.Tracks.Num() > 0
                ? FString::Join(B.Tracks, TEXT(", "))
                : FString(TEXT("—"));
            W.WriteLine(FString::Printf(TEXT("| `%s` | %s | %s | %s |"), *B.Name, *B.Kind, *Cls, *Tracks));
        }

        // Director Blueprint extras.
#if WITH_EDITOR
        if (UBlueprint* Director = LS->GetDirectorBlueprint())
        {
            TArray<FString> DirFns;
            for (UEdGraph* G : Director->FunctionGraphs)
                if (G) DirFns.Add(G->GetName());
            for (UEdGraph* G : Director->UbergraphPages)
            {
                if (!G) continue;
                for (UEdGraphNode* N : G->Nodes)
                {
                    if (!N) continue;
                    const FString NClass = N->GetClass()->GetName();
                    if (NClass == TEXT("K2Node_CustomEvent") || NClass.EndsWith(TEXT("Event")))
                        DirFns.Add(N->GetNodeTitle(ENodeTitleType::MenuTitle).ToString());
                }
            }
            if (DirFns.Num() > 0)
            {
                // C# sorts with StringComparer.Ordinal (pure raw-byte).
                DirFns.Sort([](const FString& A, const FString& B){
                    return A.Compare(B, ESearchCase::CaseSensitive) < 0;
                });
                W.WriteLine();
                W.WriteHeading(2, FString::Printf(TEXT("Director Blueprint — functions / events (%d)"), DirFns.Num()));
                W.WriteLine();
                for (const FString& F : DirFns)
                    W.WriteLine(FString::Printf(TEXT("- `%s`"), *F));
            }

            TArray<FString> TagRefs;
            TSet<FString> SeenTags;
            auto ScanPins = [&](UEdGraph* G)
            {
                if (!G) return;
                for (UEdGraphNode* N : G->Nodes)
                {
                    if (!N) continue;
                    for (UEdGraphPin* Pin : N->Pins)
                    {
                        if (!Pin || Pin->DefaultValue.IsEmpty()) continue;
                        int32 Start = 0;
                        while ((Start = Pin->DefaultValue.Find(TEXT("TagName=\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start)) != INDEX_NONE)
                        {
                            const int32 NameFrom = Start + 9;
                            const int32 End = Pin->DefaultValue.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, NameFrom);
                            if (End <= NameFrom) break;
                            const FString Tag = Pin->DefaultValue.Mid(NameFrom, End - NameFrom);
                            if (!Tag.IsEmpty() && !SeenTags.Contains(Tag))
                            {
                                SeenTags.Add(Tag);
                                TagRefs.Add(Tag);
                            }
                            Start = End + 1;
                        }
                    }
                }
            };
            for (UEdGraph* G : Director->FunctionGraphs) ScanPins(G);
            for (UEdGraph* G : Director->UbergraphPages) ScanPins(G);
            for (const FBPVariableDescription& V : Director->NewVariables)
            {
                if (V.DefaultValue.IsEmpty()) continue;
                int32 Start = 0;
                while ((Start = V.DefaultValue.Find(TEXT("TagName=\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start)) != INDEX_NONE)
                {
                    const int32 NameFrom = Start + 9;
                    const int32 End = V.DefaultValue.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, NameFrom);
                    if (End <= NameFrom) break;
                    const FString Tag = V.DefaultValue.Mid(NameFrom, End - NameFrom);
                    if (!Tag.IsEmpty() && !SeenTags.Contains(Tag))
                    {
                        SeenTags.Add(Tag);
                        TagRefs.Add(Tag);
                    }
                    Start = End + 1;
                }
            }
            if (TagRefs.Num() > 0)
            {
                TagRefs.Sort([](const FString& A, const FString& B){
                    return A.Compare(B, ESearchCase::CaseSensitive) < 0;
                });
                W.WriteLine();
                W.WriteHeading(2, FString::Printf(TEXT("GameplayTags referenced by Director BP (%d)"), TagRefs.Num()));
                W.WriteLine();
                for (const FString& Tag : TagRefs)
                    W.WriteLine(FString::Printf(TEXT("- `%s`"), *Tag));
            }
        }
#endif

        FBPeekUsedBy::Write(W, Refs, AssetPath, KnownNormalized);
    }
};