#pragma once
#include "CoreMinimal.h"
#include "BPeekMarkdownWriter.h"
#include "BPeekAssetPathHelpers.h"
#include "BPeekUsedBy.h"
#include "Engine/UserDefinedEnum.h"

/**
 * One-asset writer. Takes the UEnum and its filesystem-relative uasset path
 * (e.g. "Content/.../X.uasset"), writes a markdown document into <W>. Caller
 * owns <W> and calls SaveTo() once all desired assets have been appended.
 * <UassetRel> может быть пустым — в этом случае строка "uasset" пропускается.
 */
class FBPeekEnumWriter
{
public:
    static void Write(FBPeekMarkdownWriter& W, UEnum* E, const FString& UassetRel,
                      const TMap<FString, TArray<FString>>& Refs = TMap<FString, TArray<FString>>(),
                      const TSet<FString>& KnownNormalized = TSet<FString>(),
                      bool bVerboseMode = false)
    {
        if (!E) return;
        const FString AssetPath = E->GetPathName();
        const FString DisplayPath = bVerboseMode ? AssetPath : FBPeekAssetPath::Compact(AssetPath);
        W.WriteHeading(1, FString::Printf(TEXT("%s (Enum)"), *E->GetName()));
        W.WriteLine();
        W.WriteMetaRowCode(TEXT("Asset path"), DisplayPath);
        W.WriteMetaRowCode(TEXT("uasset"), UassetRel.IsEmpty() ? DisplayPath : UassetRel);
        W.WriteLine();

        int32 Count = E->NumEnums();
        if (Count > 0 && E->GetNameStringByIndex(Count - 1).EndsWith(TEXT("_MAX"))) Count--;

        // Collect the display labels once — both layouts need them.
        TArray<FString> Labels;
        Labels.Reserve(Count);
        for (int32 i = 0; i < Count; ++i)
        {
            FString Label = E->GetDisplayNameTextByIndex(i).ToString();
            if (Label.IsEmpty())
            {
                Label = E->GetNameStringByIndex(i);
                int32 ColonIdx;
                if (Label.FindChar(TEXT(':'), ColonIdx)) Label = Label.Mid(ColonIdx + 2);
            }
            Labels.Add(MoveTemp(Label));
        }

        if (bVerboseMode)
        {
            // Verbose — human-readable list with H2 heading + bullets.
            W.WriteHeading(2, FString::Printf(TEXT("Values (%d)"), Count));
            W.WriteLine();
            for (const FString& L : Labels)
                W.WriteLine(FString::Printf(TEXT("- `%s`"), *L));
        }
        else
        {
            // Default (AI-optimised) — one-line summary.
            TArray<FString> Quoted;
            Quoted.Reserve(Labels.Num());
            for (const FString& L : Labels) Quoted.Add(FString::Printf(TEXT("`%s`"), *L));
            W.WriteLine(FString::Printf(TEXT("**Values (%d):** %s"),
                Count, *FString::Join(Quoted, TEXT(", "))));
            W.WriteLine();
        }

        FBPeekUsedBy::Write(W, Refs, AssetPath, KnownNormalized);
    }
};