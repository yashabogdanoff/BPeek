#pragma once
#include "CoreMinimal.h"
#include "BPeekMarkdownWriter.h"
#include "BPeekAssetPathHelpers.h"
#include "BPeekUsedBy.h"
#include "BPeekCompat.h"
#include "UObject/UnrealType.h"

/**
 * One-asset writer for UserDefinedStruct. Produces # heading / asset path /
 * Fields(N) table. <UassetRel> пусто — commandlet не знает filesystem-paths.
 */
class FBPeekStructWriter
{
public:
    static void Write(FBPeekMarkdownWriter& W, UUserDefinedStruct* S, const FString& UassetRel,
                      const TMap<FString, TArray<FString>>& Refs = TMap<FString, TArray<FString>>(),
                      const TSet<FString>& KnownNormalized = TSet<FString>(),
                      bool bVerboseMode = false)
    {
        if (!S) return;
        const FString AssetPath = S->GetPathName();
        const FString DisplayPath = bVerboseMode ? AssetPath : FBPeekAssetPath::Compact(AssetPath);
        W.WriteHeading(1, FString::Printf(TEXT("%s (Struct)"), *S->GetName()));
        W.WriteLine();
        W.WriteMetaRowCode(TEXT("Asset path"), DisplayPath);
        W.WriteMetaRowCode(TEXT("uasset"), UassetRel.IsEmpty() ? DisplayPath : UassetRel);
        W.WriteLine();

        // Two-pass: collect then render so the heading count matches and
        // the default one-line form can join them together.
        TArray<TPair<FString, FString>> Fields;
        for (TFieldIterator<FProperty> It(S); It; ++It)
            Fields.Emplace(It->GetAuthoredName(), It->GetCPPType());

        if (bVerboseMode)
        {
            W.WriteHeading(2, FString::Printf(TEXT("Fields (%d)"), Fields.Num()));
            W.WriteLine();
            W.WriteLine(TEXT("| Field | Type |"));
            W.WriteLine(TEXT("|---|---|"));
            for (const auto& F : Fields)
                W.WriteLine(FString::Printf(TEXT("| `%s` | `%s` |"), *F.Key, *F.Value));
        }
        else
        {
            TArray<FString> Parts;
            Parts.Reserve(Fields.Num());
            for (const auto& F : Fields)
                Parts.Add(FString::Printf(TEXT("`%s`:`%s`"), *F.Key, *F.Value));
            W.WriteLine(FString::Printf(TEXT("**Fields (%d):** %s"),
                Fields.Num(), *FString::Join(Parts, TEXT(", "))));
            W.WriteLine();
        }

        FBPeekUsedBy::Write(W, Refs, AssetPath, KnownNormalized);
    }
};