#pragma once

#include "CoreMinimal.h"
#include "BPeekExtensionAPI.h"
#include "BPeekMarkdownWriter.h"
#include "BPeekTextUnwrap.h"
#include "BPeekAssetLinks.h"
#include "BPeekAssetPathHelpers.h"
#include "BPeekUsedBy.h"
#include "BPeekIndexBuilder.h"
#include "BPeekInputMappingsParser.h"

#include "Engine/DataAsset.h"
#include "UObject/UnrealType.h"
#include "UObject/PrimaryAssetId.h"
#include "InputMappingContext.h"

//
// Rich markdown for UInputMappingContext — emits a (Key|Action|Modifiers)
// table in addition to the usual DataAsset metadata/properties block.
//
// The Write body collects editable properties the same way the generic
// DataAsset renderer does (Properties array + sort + emit) and then
// appends the Mappings table specific to IMC. The duplication with
// FBPeekDataAssetWriter is deliberate: factoring a shared helper would
// expose ~30 lines of internal API for a marginal line-count win.
//
// When this module is disabled (EnhancedInput plugin not detected at
// build time), IMC assets still get scanned — they fall through to
// FBPeekDataAssetExtension at priority 0 and produce the generic
// DataAsset output without the Mappings table.
//
class FBPeekEnhancedInputExtension : public IBPeekExtension
{
public:
    FName   GetId()          const override { return TEXT("bpeek.enhancedinput.imc"); }
    FString GetVersionName() const override { return TEXT("1.0.0"); }
    int32   GetPriority()    const override { return 200; }   // beats generic DataAsset (0)

    TArray<UClass*> GetHandledClasses() const override
    {
        return { UInputMappingContext::StaticClass() };
    }

    bool CanHandle(UObject* Asset) const override
    {
        UInputMappingContext* IMC = Cast<UInputMappingContext>(Asset);
        if (!IMC) return false;
        if (IMC->HasAnyFlags(RF_ClassDefaultObject)) return false;
        return true;
    }

    void Write(FBPeekMarkdownWriter& W, UObject* Asset, const FBPeekScanContext& Ctx) override
    {
        UInputMappingContext* IMC = Cast<UInputMappingContext>(Asset);
        if (!IMC) return;

        // UInputMappingContext is a UDataAsset subclass — treat it like
        // DataAsset + the specialised Mappings table.
        UDataAsset* DA = static_cast<UDataAsset*>(IMC);
        const FString AssetPath = DA->GetPathName();
        const FString DisplayPath = Ctx.bVerboseMode ? AssetPath : FBPeekAssetPath::Compact(AssetPath);

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

        // Properties collection — same filter as the C# JSON dump (and
        // what the core DataAsset writer still uses for non-IMC assets).
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

        // --- Mappings table (the whole point of this extension) --------
        // UE 5.4: property "Mappings" (TArray<FEnhancedActionKeyMapping>).
        // UE 5.7: property renamed to "DefaultKeyMappings" and wrapped in a
        // FInputMappingContextMappingData struct, so the serialised value is
        // `(Mappings=((...)))` — one nesting level deeper. Parser expects
        // the flat tuple, so strip the `(Mappings=...)` wrapper when we see
        // the 5.7 shape.
        TArray<FBPeekMappingEntry> ParsedMappings;
        for (const FProp& P : Props)
        {
            if (P.Name != TEXT("Mappings") && P.Name != TEXT("DefaultKeyMappings")) continue;
            FString Raw = P.Value;
            if (P.Name == TEXT("DefaultKeyMappings"))
            {
                FString Trimmed = Raw;
                Trimmed.TrimStartAndEndInline();
                const FString Prefix(TEXT("(Mappings="));
                if (Trimmed.StartsWith(Prefix) && Trimmed.EndsWith(TEXT(")")))
                    Raw = Trimmed.Mid(Prefix.Len(), Trimmed.Len() - Prefix.Len() - 1);
            }
            ParsedMappings = FBPeekInputMappings::TryParse(Raw);
            if (ParsedMappings.Num() > 0) break;
        }

        if (ParsedMappings.Num() > 0)
        {
            if (Ctx.bVerboseMode)
            {
                W.WriteHeading(2, FString::Printf(TEXT("Mappings (%d)"), ParsedMappings.Num()));
                W.WriteLine();
                W.WriteLine(TEXT("| Key | Action | Modifiers |"));
                W.WriteLine(TEXT("|---|---|---|"));
                for (const FBPeekMappingEntry& M : ParsedMappings)
                {
                    const FString Mods = M.Modifiers.Num() > 0
                        ? FString::Join(M.Modifiers, TEXT(", "))
                        : FString(TEXT("—"));
                    W.WriteLine(FString::Printf(TEXT("| `%s` | `%s` | %s |"), *M.Key, *M.Action, *Mods));
                }
                W.WriteLine();
            }
            else
            {
                TArray<FString> Parts;
                Parts.Reserve(ParsedMappings.Num());
                for (const FBPeekMappingEntry& M : ParsedMappings)
                {
                    FString Entry = FString::Printf(TEXT("`%s`→`%s`"), *M.Key, *M.Action);
                    if (M.Modifiers.Num() > 0)
                        Entry += FString::Printf(TEXT("{%s}"), *FString::Join(M.Modifiers, TEXT(";")));
                    Parts.Add(MoveTemp(Entry));
                }
                W.WriteLine(FString::Printf(TEXT("**Mappings (%d):** %s"),
                    ParsedMappings.Num(), *FString::Join(Parts, TEXT(", "))));
                W.WriteLine();
            }
        }

        // --- Properties block (Mappings row suppressed when the table
        //     above rendered — avoids duplicating the raw blob).
        // Shared empties keep the function-local usages below tidy when
        // Ctx.Refs/Ctx.Known are null (synthetic contexts / tests).
        static const TMap<FString, TArray<FString>> EmptyRefs;
        static const TSet<FString> EmptyKnown;
        const TSet<FString>& Known = Ctx.Known ? *Ctx.Known : EmptyKnown;

        if (Props.Num() == 0)
        {
            W.WriteLine(TEXT("_No editable properties._"));
        }
        else
        {
            Props.Sort([](const FProp& A, const FProp& B){
                return A.Name.Compare(B.Name, ESearchCase::CaseSensitive) < 0;
            });

            if (Ctx.bVerboseMode)
            {
                W.WriteHeading(2, TEXT("Properties"));
                W.WriteLine();
                for (const FProp& P : Props)
                {
                    if (ParsedMappings.Num() > 0 && (P.Name == TEXT("Mappings") || P.Name == TEXT("DefaultKeyMappings"))) continue;
                    FString V = FBPeekTextUnwrap::Unwrap(P.Value);
                    V = FBPeekAssetLinks::Linkify(V, AssetPath, Known);
                    W.WriteLine(FString::Printf(TEXT("- **%s**: %s"), *P.Name, *V));
                }
            }
            else
            {
                TArray<FString> Parts;
                for (const FProp& P : Props)
                {
                    if (ParsedMappings.Num() > 0 && (P.Name == TEXT("Mappings") || P.Name == TEXT("DefaultKeyMappings"))) continue;
                    FString V = FBPeekTextUnwrap::Unwrap(P.Value);
                    V = FBPeekAssetLinks::Linkify(V, AssetPath, Known);
                    V.ReplaceInline(TEXT(","), TEXT(";"));
                    Parts.Add(FString::Printf(TEXT("`%s`=%s"), *P.Name, *V));
                }
                if (Parts.Num() > 0)
                {
                    W.WriteLine(FString::Printf(TEXT("**Properties (%d):** %s"),
                        Parts.Num(), *FString::Join(Parts, TEXT(", "))));
                    W.WriteLine();
                }
            }
        }

        FBPeekUsedBy::Write(W,
            Ctx.Refs ? *Ctx.Refs : EmptyRefs,
            AssetPath, Known);
    }

    void AppendToIndex(FBPeekIndexBuilder& IB, UObject* Asset) const override
    {
        if (UDataAsset* DA = Cast<UDataAsset>(Asset)) IB.AddDataAsset(DA);
    }
};
