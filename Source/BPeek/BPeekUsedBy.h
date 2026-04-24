#pragma once
#include "CoreMinimal.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "BPeekMarkdownWriter.h"
#include "BPeekAssetPathHelpers.h"

class FBPeekUsedBy
{
public:
    /**
     * Append a "## Used by (N)" section for <AssetPath>.
     *
     * <Refs> is a map keyed by normalized asset path (no `.Name` suffix) →
     * list of normalized referrer paths. Built once per run in the
     * commandlet (AssetRegistry hard deps + property-text scan + widget
     * tree walk) so writers see the same view the C# renderer does.
     * Caller passes an empty map for asset types with no inbound refs.
     */
    static void Write(FBPeekMarkdownWriter& W,
                      const TMap<FString, TArray<FString>>& Refs,
                      const FString& AssetPath,
                      const TSet<FString>& KnownNormalized = TSet<FString>())
    {
        const FString Normalized = FBPeekAssetPath::Normalize(AssetPath);
        const TArray<FString>* Entries = Refs.Find(Normalized);
        if (!Entries || Entries->Num() == 0) return;

        TArray<FString> Kept;
        Kept.Reserve(Entries->Num());
        for (const FString& R : *Entries)
        {
            const FString Norm = FBPeekAssetPath::Normalize(R);
            if (Norm.Equals(Normalized)) continue;  // self-skip
            Kept.AddUnique(Norm);
        }
        if (Kept.Num() == 0) return;

        Kept.Sort([](const FString& A, const FString& B){
            return FBPeekAssetPath::OrdinalIgnoreCaseCompare(A, B) < 0;
        });

        W.WriteLine();
        W.WriteHeading(2, FString::Printf(TEXT("Referenced by (%d)"), Kept.Num()));
        W.WriteLine();
        const bool bHasKnown = KnownNormalized.Num() > 0;
        for (const FString& R : Kept)
        {
            const FString Name = FBPeekAssetPath::ShortName(R);
            // Known-set gate: referencer not in the known dump → emit the
            // short name only, no MD link. Matches C# FormatLink semantics
            // for non-null known.
            if (bHasKnown && !KnownNormalized.Contains(R))
            {
                W.WriteLine(FString::Printf(TEXT("- %s"), *Name));
                continue;
            }
            const FString Rel = FBPeekAssetPath::RelativeMdPath(AssetPath, R);
            W.WriteLine(FString::Printf(TEXT("- [%s](%s)"), *Name, *Rel));
        }
    }
};