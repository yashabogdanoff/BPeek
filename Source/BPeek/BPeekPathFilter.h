#pragma once
#include "CoreMinimal.h"
#include "BPeekLog.h"
#include "Misc/FileHelper.h"
#include "Internationalization/Regex.h"

//
// Glob-pattern matcher for MD-emission filtering. Two independent lists:
//   - Include: if non-empty, only paths matching at least one pattern pass.
//   - Exclude: paths matching any exclude pattern are rejected (applied
//              after the include pass).
//
// Pattern syntax (same as the existing examples/*.txt files):
//   - '#' starts a comment. Blank lines ignored.
//   - '?'  single char (not '/').
//   - '*'  any chars up to the next '/'.
//   - '**' any chars including '/' (multi-segment wildcard).
//
// Patterns are anchored to the start of the asset path. Use a leading
// '**' followed by '/' to match anywhere in the path. Examples:
//
//   /Engine/<double-star>              exact prefix /Engine/
//   <double-star>/Engine/<double-star> contains /Engine/ anywhere
//   /Game/UI/<double-star>             only the /Game/UI/ subtree
//
// Implementation translates each glob into an anchored regex once on
// load, then ShouldInclude runs per-asset matches. Fast enough for
// project-scale (under a hundred patterns by ~1200 assets).
//
class FBPeekPathFilter
{
public:
    /** Reset state and parse pipe (`\n` or `;`) separated pattern text.
     *  Returns the number of active patterns loaded (0 = no-op filter). */
    int32 LoadInclude(const FString& Text) { return Load(IncludeRules, Text); }
    int32 LoadExclude(const FString& Text) { return Load(ExcludeRules, Text); }

    /** Load patterns from an on-disk text file. Missing/empty file = no-op. */
    int32 LoadIncludeFromFile(const FString& Path)
    {
        FString S;
        if (!FFileHelper::LoadFileToString(S, *Path)) return 0;
        return LoadInclude(S);
    }
    int32 LoadExcludeFromFile(const FString& Path)
    {
        FString S;
        if (!FFileHelper::LoadFileToString(S, *Path)) return 0;
        return LoadExclude(S);
    }

    /** True if the asset path survives the include + exclude passes. */
    bool ShouldInclude(const FString& AssetPath) const
    {
        if (IncludeRules.Num() > 0 && !MatchesAny(IncludeRules, AssetPath))
            return false;
        if (ExcludeRules.Num() > 0 && MatchesAny(ExcludeRules, AssetPath))
            return false;
        return true;
    }

    bool IsActive() const { return IncludeRules.Num() > 0 || ExcludeRules.Num() > 0; }
    int32 NumInclude() const { return IncludeRules.Num(); }
    int32 NumExclude() const { return ExcludeRules.Num(); }

private:
    struct FRule
    {
        FString Raw;
        TSharedPtr<FRegexPattern> Pattern;
    };
    TArray<FRule> IncludeRules;
    TArray<FRule> ExcludeRules;

    static int32 Load(TArray<FRule>& Out, const FString& Text)
    {
        Out.Reset();
        TArray<FString> Lines;
        Text.ParseIntoArray(Lines, TEXT("\n"), /*bCullEmpty=*/false);
        int32 LineNumber = 0;
        for (FString Line : Lines)
        {
            ++LineNumber;
            const FString Original = Line;
            Line.TrimStartAndEndInline();
            if (Line.IsEmpty() || Line.StartsWith(TEXT("#"))) continue;

            // Validation — log warnings for syntax we don't support so the
            // user can fix bad lines instead of silently missing matches.

            if (Line.StartsWith(TEXT("!")))
            {
                UE_LOG(LogBPeek, Warning,
                    TEXT("[BPeek filter] line %d: '!' negation prefix is NOT supported, "
                         "rule '%s' will be taken literally (probably a no-op)"),
                    LineNumber, *Line);
            }

            if (CountChar(Line, TEXT('[')) != CountChar(Line, TEXT(']')))
            {
                UE_LOG(LogBPeek, Warning,
                    TEXT("[BPeek filter] line %d: unbalanced '[' / ']' in rule '%s' — skipped"),
                    LineNumber, *Line);
                continue;
            }

            // Spaces aren't syntactically invalid but are a common typo
            // (trailing space on a path). Warn, but accept — some asset
            // names legitimately have spaces.
            if (Original.EndsWith(TEXT(" ")) || Original.EndsWith(TEXT("\t")))
            {
                UE_LOG(LogBPeek, Warning,
                    TEXT("[BPeek filter] line %d: trailing whitespace on rule '%s' — trimmed"),
                    LineNumber, *Line);
            }

            FRule R;
            R.Raw = Line;
            R.Pattern = MakeShared<FRegexPattern>(GlobToRegex(Line));
            Out.Add(MoveTemp(R));
            UE_LOG(LogBPeek, Verbose, TEXT("[BPeek filter] loaded rule: %s"), *Line);
        }
        return Out.Num();
    }

    static int32 CountChar(const FString& S, TCHAR C)
    {
        int32 N = 0;
        for (TCHAR X : S) if (X == C) ++N;
        return N;
    }

    static bool MatchesAny(const TArray<FRule>& Rules, const FString& Path)
    {
        for (const FRule& R : Rules)
        {
            if (!R.Pattern.IsValid()) continue;
            FRegexMatcher M(*R.Pattern, Path);
            if (M.FindNext()) return true;
        }
        return false;
    }

    /** Translate a glob to an anchored regex. Supports **, *, ?; escapes
     *  other regex meta-chars. */
    static FString GlobToRegex(const FString& Glob)
    {
        FString Out;
        Out.Reserve(Glob.Len() * 2 + 4);
        Out += TEXT("^");
        for (int32 i = 0; i < Glob.Len(); ++i)
        {
            const TCHAR C = Glob[i];
            if (C == TEXT('*'))
            {
                if (i + 1 < Glob.Len() && Glob[i + 1] == TEXT('*'))
                {
                    // ** matches any run of chars including /
                    Out += TEXT(".*");
                    ++i;
                }
                else
                {
                    Out += TEXT("[^/]*");
                }
            }
            else if (C == TEXT('?'))
            {
                Out += TEXT("[^/]");
            }
            else if (IsRegexSpecial(C))
            {
                Out.AppendChar(TEXT('\\'));
                Out.AppendChar(C);
            }
            else
            {
                Out.AppendChar(C);
            }
        }
        Out += TEXT("$");
        return Out;
    }

    static bool IsRegexSpecial(TCHAR C)
    {
        switch (C)
        {
            case TEXT('.'): case TEXT('\\'): case TEXT('+'):
            case TEXT('('): case TEXT(')'):  case TEXT('{'):
            case TEXT('}'): case TEXT('['):  case TEXT(']'):
            case TEXT('|'): case TEXT('^'):  case TEXT('$'):
                return true;
            default:
                return false;
        }
    }
};
