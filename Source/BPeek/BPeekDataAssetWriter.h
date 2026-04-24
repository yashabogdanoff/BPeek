#pragma once
#include "CoreMinimal.h"
#include "BPeekMarkdownWriter.h"
#include "BPeekTextUnwrap.h"
#include "BPeekAssetLinks.h"
#include "BPeekAssetPathHelpers.h"
#include "BPeekUsedBy.h"
#include "Engine/DataAsset.h"
#include "UObject/UnrealType.h"
#include "UObject/PrimaryAssetId.h"

class FBPeekDataAssetWriter
{
public:
    static void Write(FBPeekMarkdownWriter& W, UDataAsset* DA,
                      const TMap<FString, TArray<FString>>& Refs = TMap<FString, TArray<FString>>(),
                      const TSet<FString>& KnownNormalized = TSet<FString>(),
                      bool bVerboseMode = false)
    {
        if (!DA) return;
        const FString AssetPath = DA->GetPathName();
        const FString DisplayPath = bVerboseMode ? AssetPath : FBPeekAssetPath::Compact(AssetPath);

        W.WriteHeading(1, FString::Printf(TEXT("%s (Data Asset)"), *DA->GetName()));
        W.WriteLine();
        W.WriteMetaRowCode(TEXT("Asset path"), DisplayPath);
        W.WriteMetaRowCode(TEXT("Class"), DA->GetClass()->GetName());
        if (UPrimaryDataAsset* PDA = Cast<UPrimaryDataAsset>(DA))
        {
            const FPrimaryAssetId PID = PDA->GetPrimaryAssetId();
            if (PID.IsValid())
                W.WriteMetaRowCode(TEXT("Primary asset type"), PID.PrimaryAssetType.ToString());
        }
        W.WriteLine();

        // Collect editable non-base properties — same filter as the JSON dump.
        struct FProp { FString Name; FString Value; };
        TArray<FProp> Props;
        for (TFieldIterator<FProperty> PIt(DA->GetClass()); PIt; ++PIt)
        {
            FProperty* P = *PIt;
            if (!P->HasAnyPropertyFlags(CPF_Edit)) continue;
            if (P->HasAnyPropertyFlags(CPF_Transient)) continue;
            if (P->GetOwnerClass() == UDataAsset::StaticClass()) continue;
            if (P->GetOwnerClass() == UPrimaryDataAsset::StaticClass()) continue;
            const FString PropName = P->GetName();
            if (PropName.StartsWith(TEXT("bOverride_")))
            {
                if (FBoolProperty* BP = CastField<FBoolProperty>(P);
                    BP && !BP->GetPropertyValue_InContainer(DA)) continue;
            }
            FString Val;
            if (FObjectProperty* OP = CastField<FObjectProperty>(P))
            {
                UObject* V = OP->GetObjectPropertyValue_InContainer(DA);
                if (V) Val = V->GetPathName();
            }
            else if (FSoftObjectProperty* SP = CastField<FSoftObjectProperty>(P))
            {
                const FSoftObjectPtr V = SP->GetPropertyValue_InContainer(DA);
                if (!V.IsNull()) Val = V.ToString();
            }
            else if (FTextProperty* TP = CastField<FTextProperty>(P))
            {
                const FText V = TP->GetPropertyValue_InContainer(DA);
                if (!V.IsEmpty()) Val = V.ToString();
            }
            else
            {
                P->ExportTextItem_Direct(Val, P->ContainerPtrToValuePtr<void>(DA), nullptr, nullptr, PPF_None);
                if (Val == TEXT("None") || Val == TEXT("\"\"") || Val == TEXT("()"))
                    Val = FString();
            }
            if (Val.IsEmpty()) continue;
            Props.Add({ PropName, Val });
        }

        // Note: InputMappingContext-specific rendering (key/action/
        // modifier table) lives in the BPeekEnhancedInput submodule.
        // When that submodule is disabled (EnhancedInput plugin not
        // detected at build time), IMCs fall through to this generic
        // writer and their `Mappings` property renders as raw struct
        // text.
        if (Props.Num() == 0)
        {
            W.WriteLine(TEXT("_No editable properties._"));
        }
        else
        {
            // Ordinal case-sensitive sort — matches C# OrderBy default
            // (uppercase before lowercase).
            Props.Sort([](const FProp& A, const FProp& B){
                return A.Name.Compare(B.Name, ESearchCase::CaseSensitive) < 0;
            });

            if (bVerboseMode)
            {
                W.WriteHeading(2, TEXT("Properties"));
                W.WriteLine();
                for (const FProp& P : Props)
                {
                    FString V = FBPeekTextUnwrap::Unwrap(P.Value);
                    V = FBPeekAssetLinks::Linkify(V, AssetPath, KnownNormalized);
                    W.WriteLine(FString::Printf(TEXT("- **%s**: %s"), *P.Name, *V));
                }
            }
            else
            {
                TArray<FString> Parts;
                Parts.Reserve(Props.Num());
                for (const FProp& P : Props)
                {
                    FString V = FBPeekTextUnwrap::Unwrap(P.Value);
                    V = FBPeekAssetLinks::Linkify(V, AssetPath, KnownNormalized);
                    V.ReplaceInline(TEXT(","), TEXT(";"));
                    Parts.Add(FString::Printf(TEXT("`%s`=%s"), *P.Name, *V));
                }
                W.WriteLine(FString::Printf(TEXT("**Properties (%d):** %s"),
                    Props.Num(), *FString::Join(Parts, TEXT(", "))));
                W.WriteLine();
            }
        }

        FBPeekUsedBy::Write(W, Refs, AssetPath, KnownNormalized);
    }
};