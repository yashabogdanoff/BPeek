#pragma once
#include "CoreMinimal.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Internationalization/Regex.h"
#include "BPeekMarkdownWriter.h"
#include "BPeekAssetPathHelpers.h"

struct FBPeekGameplayTag
{
    FString Name;
    FString DevComment;  // empty if none
    FString Source;      // "ini" or "runtime"
    TArray<FString> UsedBy;
};

class FBPeekGameplayTagsWriter
{
public:
    /**
     * Parse <ProjectRoot>/Config/DefaultGameplayTags.ini and return
     * declared tags with DevComment. Returns empty if the ini doesn't
     * exist (matches C# behavior).
     */
    static TMap<FString, FBPeekGameplayTag> LoadFromIni(const FString& ProjectRoot)
    {
        TMap<FString, FBPeekGameplayTag> Out;
        const FString Path = FPaths::Combine(ProjectRoot, TEXT("Config"), TEXT("DefaultGameplayTags.ini"));
        if (!FPaths::FileExists(Path)) return Out;
        TArray<FString> Lines;
        FFileHelper::LoadFileToStringArray(Lines, *Path);

        static const FRegexPattern Pattern(FString(TEXT(
            "\\+GameplayTagList=\\(Tag=\"([^\"]+)\"(?:,DevComment=\"([^\"]*)\")?")));
        for (const FString& Line : Lines)
        {
            FRegexMatcher M(Pattern, Line);
            if (!M.FindNext()) continue;
            FBPeekGameplayTag T;
            T.Name = M.GetCaptureGroup(1);
            T.DevComment = M.GetCaptureGroup(2);
            T.Source = TEXT("ini");
            Out.Add(T.Name, T);
        }
        return Out;
    }

    /**
     * Extract TagName="…" literals from an arbitrary string. Caller passes
     * each hit to the aggregator. Same family as GameplayTagsLoader.cs.
     */
    static TArray<FString> ExtractTagLiterals(const FString& Raw)
    {
        TArray<FString> Out;
        if (Raw.IsEmpty()) return Out;
        int32 Start = 0;
        while (true)
        {
            Start = Raw.Find(TEXT("TagName=\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
            if (Start == INDEX_NONE) break;
            const int32 NameFrom = Start + 9;
            const int32 End = Raw.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, NameFrom);
            if (End <= NameFrom) break;
            Out.Add(Raw.Mid(NameFrom, End - NameFrom));
            Start = End + 1;
        }
        return Out;
    }

    /**
     * Render the aggregated map to <W>. Grouping by namespace (first-dot
     * split) with ordinal sort on the namespace key, ordinal sort on tags
     * within each group. Matches GameplayTagsRenderer.WriteAll.
     */
    static void WriteAll(FBPeekMarkdownWriter& W,
                        const TMap<FString, FBPeekGameplayTag>& Tags)
    {
        int32 IniCount = 0, RuntimeCount = 0;
        for (const auto& KV : Tags)
            (KV.Value.Source == TEXT("ini") ? IniCount : RuntimeCount) += 1;

        W.WriteHeading(1, TEXT("GameplayTags"));
        W.WriteLine();
        W.WriteLine(FString::Printf(
            TEXT("Project-wide gameplay tags: **%d** total "
                 "(%d from DefaultGameplayTags.ini, %d discovered via usage)."),
            Tags.Num(), IniCount, RuntimeCount));
        W.WriteLine();

        // Group by namespace (first-dot prefix, "(root)" if no dot).
        // Store copies rather than pointers — TArray::Sort on pointer
        // element types trips nested-struct predicate resolution in UE 5.4.
        TMap<FString, TArray<FBPeekGameplayTag>> Groups;
        for (const auto& KV : Tags)
        {
            const FString& N = KV.Value.Name;
            int32 Dot;
            FString Key = (N.FindChar(TEXT('.'), Dot) && Dot > 0)
                ? N.Left(Dot)
                : FString(TEXT("(root)"));
            Groups.FindOrAdd(Key).Add(KV.Value);
        }

        TArray<FString> Keys;
        Groups.GetKeys(Keys);
        Keys.Sort([](const FString& A, const FString& B){
            return A.Compare(B, ESearchCase::CaseSensitive) < 0;
        });

        for (const FString& Key : Keys)
        {
            TArray<FBPeekGameplayTag>& Items = Groups[Key];
            Items.Sort([](const FBPeekGameplayTag& A, const FBPeekGameplayTag& B){
                return A.Name.Compare(B.Name, ESearchCase::CaseSensitive) < 0;
            });
            W.WriteHeading(2, FString::Printf(TEXT("%s (%d)"), *Key, Items.Num()));
            W.WriteLine();
            for (const FBPeekGameplayTag& T : Items)
            {
                const FString Src = T.Source == TEXT("runtime") ? FString(TEXT(" _(runtime)_")) : FString();
                FString Line = FString::Printf(TEXT("- **`%s`**%s"), *T.Name, *Src);
                if (!T.DevComment.IsEmpty())
                    Line += FString::Printf(TEXT(" — %s"), *T.DevComment);
                W.WriteLine(Line);

                if (T.UsedBy.Num() > 0)
                {
                    TArray<FString> Dedup;
                    for (const FString& U : T.UsedBy) Dedup.AddUnique(U);
                    Dedup.Sort([](const FString& A, const FString& B){
                        return FBPeekAssetPath::OrdinalIgnoreCaseCompare(A, B) < 0;
                    });
                    // Used-by entries have no KnownSet gate on the C# side
                    // (FormatLink falls back to name-only for unknowns), so
                    // mirror that by emitting linked names unconditionally.
                    for (const FString& U : Dedup)
                    {
                        const FString Name = FBPeekAssetPath::ShortName(U);
                        const FString Normalized = FBPeekAssetPath::Normalize(U);
                        // Root file — no AssetPath context, so use empty "from"
                        // and compute rel from project root.
                        const FString Rel = FBPeekAssetPath::ToMdSubpath(Normalized);
                        W.WriteLine(FString::Printf(TEXT("  - used by [%s](%s)"), *Name, *Rel));
                    }
                }
            }
            W.WriteLine();
        }
    }
};