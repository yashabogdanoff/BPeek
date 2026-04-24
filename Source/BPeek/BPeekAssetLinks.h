#pragma once
#include "CoreMinimal.h"
#include "Internationalization/Regex.h"
#include "BPeekAssetPathHelpers.h"

class FBPeekAssetLinks
{
public:
    /**
     * Replace every UE-path occurrence in <Raw> that resolves to an asset
     * in <KnownNormalized> with a markdown link. <CurrentAssetPath> is the
     * asset whose MD is being rendered — used as the "from" of the relative
     * path. Paths not in the known set are kept verbatim, matching C# when
     * RenderContext.Known is populated.
     */
    static FString Linkify(const FString& Raw,
                           const FString& CurrentAssetPath,
                           const TSet<FString>& KnownNormalized)
    {
        if (Raw.IsEmpty() || KnownNormalized.Num() == 0) return Raw;

        // Path segment = Unicode letter, number, underscore or dash. Mirrors
        // the C# AssetLinks pattern `[\p{L}\p{N}_\-]`. Using the ICU property
        // class keeps `.`, `:`, `/` and other punctuation *out*, which is how
        // C# naturally stops inside strings like
        // `.../IA_Look.IA_Look:InputModifierScalar_0'` — the `:` kills the
        // segment and the lookalike terminator check below then rejects the
        // partial match.
        static const FRegexPattern Pattern(FString(TEXT(
            "/(?:Game|Module_[\\p{L}\\p{N}_]+|Plugins/[^/]+)"
            "(?:/[\\p{L}\\p{N}_\\-]+)+"
            "(?:\\.[\\p{L}\\p{N}_]+(?:_C)?)?")));
        FRegexMatcher M(Pattern, Raw);

        FString Out;
        Out.Reserve(Raw.Len());
        int32 LastEnd = 0;
        while (M.FindNext())
        {
            const int32 S = M.GetMatchBeginning();
            const int32 E = M.GetMatchEnding();

            // Mimic the C# lookahead `(?=['"\s,\)\]\}]|$)` — match must be
            // followed by a terminator, otherwise it's a stray slash in the
            // middle of a word.
            if (E < Raw.Len())
            {
                const TCHAR Next = Raw[E];
                const bool bTerm =
                    Next == TEXT('"') || Next == TEXT('\'') ||
                    FChar::IsWhitespace(Next) ||
                    Next == TEXT(',') || Next == TEXT(')') ||
                    Next == TEXT(']') || Next == TEXT('}');
                if (!bTerm) continue;
            }

            FString Matched = Raw.Mid(S, E - S);
            FString Norm = FBPeekAssetPath::Normalize(Matched);
            if (!KnownNormalized.Contains(Norm)) continue;

            // Commit: text-before-match, then linkified form.
            Out.Append(Raw.Mid(LastEnd, S - LastEnd));
            const FString Name = FBPeekAssetPath::ShortName(Norm);
            const FString Rel = EscapeMdUrl(FBPeekAssetPath::RelativeMdPath(CurrentAssetPath, Norm));
            Out += FString::Printf(TEXT("[%s](%s)"), *Name, *Rel);
            LastEnd = E;
        }
        Out.Append(Raw.Mid(LastEnd));
        return Out;
    }

    /**
     * Find all UE mount paths in <Raw> that pass the terminator check and
     * return them in normalized form (no `_C` / `.Name` suffix). Used by the
     * commandlet to mine cross-refs from DataAsset / Flow / Level property
     * text values — mirrors the path-extraction half of C# EnrichReferencers.
     */
    static TArray<FString> ExtractPaths(const FString& Raw)
    {
        TArray<FString> Out;
        if (Raw.IsEmpty()) return Out;

        static const FRegexPattern Pattern(FString(TEXT(
            "/(?:Game|Module_[\\p{L}\\p{N}_]+|Plugins/[^/]+)"
            "(?:/[\\p{L}\\p{N}_\\-]+)+"
            "(?:\\.[\\p{L}\\p{N}_]+(?:_C)?)?")));
        FRegexMatcher M(Pattern, Raw);
        while (M.FindNext())
        {
            const int32 S = M.GetMatchBeginning();
            const int32 E = M.GetMatchEnding();
            if (E < Raw.Len())
            {
                const TCHAR Next = Raw[E];
                const bool bTerm =
                    Next == TEXT('"') || Next == TEXT('\'') ||
                    FChar::IsWhitespace(Next) ||
                    Next == TEXT(',') || Next == TEXT(')') ||
                    Next == TEXT(']') || Next == TEXT('}');
                if (!bTerm) continue;
            }
            Out.Add(FBPeekAssetPath::Normalize(Raw.Mid(S, E - S)));
        }
        return Out;
    }

private:
    /** Escape markdown-sensitive chars in relative URL — spaces & parens. */
    static FString EscapeMdUrl(const FString& Url)
    {
        FString Out;
        Out.Reserve(Url.Len());
        for (int32 i = 0; i < Url.Len(); ++i)
        {
            const TCHAR C = Url[i];
            if      (C == TEXT(' ')) Out += TEXT("%20");
            else if (C == TEXT('(')) Out += TEXT("%28");
            else if (C == TEXT(')')) Out += TEXT("%29");
            else Out.AppendChar(C);
        }
        return Out;
    }
};