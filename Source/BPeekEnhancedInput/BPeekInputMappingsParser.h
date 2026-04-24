#pragma once
#include "CoreMinimal.h"

struct FBPeekMappingEntry
{
    FString Key;
    FString Action;
    TArray<FString> Modifiers;
};

class FBPeekInputMappings
{
public:
    /** Empty array = not a valid mappings blob (caller falls back to raw). */
    static TArray<FBPeekMappingEntry> TryParse(const FString& Raw)
    {
        TArray<FBPeekMappingEntry> Out;
        FString S = Raw; S.TrimStartAndEndInline();
        if (S.IsEmpty() || !S.StartsWith(TEXT("(")) || !S.EndsWith(TEXT(")"))) return Out;
        S = S.Mid(1, S.Len() - 2);

        TArray<FString> Entries = SplitTopLevel(S);
        if (Entries.Num() == 0) return Out;

        for (FString E : Entries)
        {
            E.TrimStartAndEndInline();
            if (E.StartsWith(TEXT("(")) && E.EndsWith(TEXT(")")))
                E = E.Mid(1, E.Len() - 2);

            TMap<FString, FString> Fields;
            for (const FString& F : SplitTopLevel(E))
            {
                int32 Eq;
                if (!F.FindChar(TEXT('='), Eq) || Eq <= 0) continue;
                Fields.Add(F.Left(Eq).TrimStartAndEnd(), F.Mid(Eq + 1).TrimStartAndEnd());
            }

            FBPeekMappingEntry Entry;
            if (const FString* K = Fields.Find(TEXT("Key"))) Entry.Key = StripQuotes(*K);
            if (const FString* A = Fields.Find(TEXT("Action"))) Entry.Action = PrettifyAction(*A);
            if (const FString* M = Fields.Find(TEXT("Modifiers"))) Entry.Modifiers = ParseModifiers(*M);
            Out.Add(Entry);
        }
        return Out;
    }

private:
    /** Split on top-level commas — ignore commas inside () or "". */
    static TArray<FString> SplitTopLevel(const FString& S)
    {
        TArray<FString> Parts;
        int32 Depth = 0;
        bool bInQuote = false;
        int32 Start = 0;
        for (int32 i = 0; i < S.Len(); ++i)
        {
            const TCHAR C = S[i];
            if (C == TEXT('"')) { bInQuote = !bInQuote; continue; }
            if (bInQuote) continue;
            if (C == TEXT('(')) ++Depth;
            else if (C == TEXT(')')) --Depth;
            else if (C == TEXT(',') && Depth == 0)
            {
                Parts.Add(S.Mid(Start, i - Start));
                Start = i + 1;
            }
        }
        if (Start < S.Len()) Parts.Add(S.Mid(Start));
        return Parts;
    }

    static TArray<FString> ParseModifiers(const FString& Raw)
    {
        TArray<FString> Result;
        FString R = Raw; R.TrimStartAndEndInline();
        if (R.StartsWith(TEXT("(")) && R.EndsWith(TEXT(")")))
            R = R.Mid(1, R.Len() - 2);
        for (FString Part : SplitTopLevel(R))
        {
            Part.TrimStartAndEndInline();
            FString P = StripQuotes(Part);
            int32 Idx = P.Find(TEXT("InputModifier"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
            if (Idx == INDEX_NONE) { Result.Add(P); continue; }
            FString Name = P.Mid(Idx + 13);  // strlen("InputModifier") = 13
            // Trim trailing quotes.
            while (Name.EndsWith(TEXT("'")) || Name.EndsWith(TEXT("\""))) Name = Name.LeftChop(1);
            // Strip "_N" instance suffix if N is numeric.
            int32 Under;
            if (Name.FindLastChar(TEXT('_'), Under) && Under > 0)
            {
                bool bAllDigits = Under + 1 < Name.Len();
                for (int32 j = Under + 1; j < Name.Len() && bAllDigits; ++j)
                    bAllDigits = FChar::IsDigit(Name[j]);
                if (bAllDigits) Name = Name.Left(Under);
            }
            Result.Add(Name);
        }
        return Result;
    }

    static FString PrettifyAction(const FString& Raw)
    {
        // "/Script/EnhancedInput.InputAction'/Game/.../IA_Move.IA_Move'" → "IA_Move"
        FString S = StripQuotes(Raw);
        while (S.EndsWith(TEXT("'")) || S.EndsWith(TEXT("\""))) S = S.LeftChop(1);
        int32 LastDot;
        if (S.FindLastChar(TEXT('.'), LastDot)) S = S.Mid(LastDot + 1);
        return S;
    }

    static FString StripQuotes(const FString& In)
    {
        FString S = In; S.TrimStartAndEndInline();
        if (S.Len() >= 2 && S.StartsWith(TEXT("\"")) && S.EndsWith(TEXT("\"")))
            S = S.Mid(1, S.Len() - 2);
        return S;
    }
};