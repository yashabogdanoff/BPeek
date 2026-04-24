#pragma once
#include "CoreMinimal.h"
#include "BPeekMarkdownWriter.h"
#include "BPeekTextUnwrap.h"
#include "BPeekAssetLinks.h"
#include "BPeekAssetPathHelpers.h"
#include "BPeekUsedBy.h"
#include "Engine/DataTable.h"
#include "UObject/UnrealType.h"

class FBPeekDataTableWriter
{
public:
    static void Write(FBPeekMarkdownWriter& W, UDataTable* DT, const FString& UassetRel,
                      const TMap<FString, TArray<FString>>& Refs = TMap<FString, TArray<FString>>(),
                      const TSet<FString>& KnownNormalized = TSet<FString>(),
                      bool bVerboseMode = false)
    {
        if (!DT) return;
        const FString AssetPath = DT->GetPathName();
        const FString DisplayPath = bVerboseMode ? AssetPath : FBPeekAssetPath::Compact(AssetPath);
        W.WriteHeading(1, FString::Printf(TEXT("%s (DataTable)"), *DT->GetName()));
        W.WriteLine();
        W.WriteMetaRowCode(TEXT("Asset path"), DisplayPath);
        W.WriteMetaRowCode(TEXT("uasset"), UassetRel.IsEmpty() ? DisplayPath : UassetRel);
        if (DT->RowStruct)
            W.WriteMetaRowCode(TEXT("Row struct"), DT->RowStruct->GetName());
        W.WriteLine();

        const TArray<FName> RowNames = DT->GetRowNames();
        if (RowNames.Num() == 0) return;

        // Collect column names from RowStruct in declaration order. C# reads
        // these from the first row's dict; order is preserved because
        // TFieldIterator and ExportTextItem_Direct walk fields in the same
        // declaration order on the commandlet side when producing JSON.
        TArray<FString> Columns;
        if (DT->RowStruct)
            for (TFieldIterator<FProperty> It(DT->RowStruct); It; ++It)
                Columns.Add(It->GetName());

        if (Columns.Num() > 0)
        {
            W.WriteHeading(2, FString::Printf(TEXT("Rows (%d)"), RowNames.Num()));
            W.WriteLine();

            // Header row.
            FString Header = TEXT("| Row |");
            for (const FString& C : Columns) Header += FString::Printf(TEXT(" %s |"), *C);
            W.WriteLine(Header);
            FString Sep = TEXT("|---|");
            for (int32 i = 0; i < Columns.Num(); ++i) Sep += TEXT("---|");
            W.WriteLine(Sep);

            // Data rows.
            for (const FName& RN : RowNames)
            {
                FString Line = FString::Printf(TEXT("| `%s` |"), *RN.ToString());
                uint8* RD = DT->FindRowUnchecked(RN);
                if (RD && DT->RowStruct)
                {
                    for (TFieldIterator<FProperty> It(DT->RowStruct); It; ++It)
                    {
                        FString Val;
                        It->ExportTextItem_Direct(Val, RD + It->GetOffset_ForInternal(), nullptr, nullptr, PPF_None);
                        Val = FBPeekTextUnwrap::Unwrap(Val);
                        Val = FBPeekAssetLinks::Linkify(Val, AssetPath, KnownNormalized);
                        // Cells can contain '|' from struct literals; escape.
                        Val.ReplaceInline(TEXT("|"), TEXT("\\|"));
                        Line += FString::Printf(TEXT(" %s |"), *Val);
                    }
                }
                else
                {
                    for (int32 i = 0; i < Columns.Num(); ++i) Line += TEXT("  |");
                }
                W.WriteLine(Line);
            }
        }
        else
        {
            W.WriteHeading(2, FString::Printf(TEXT("Row names (%d)"), RowNames.Num()));
            W.WriteLine();
            for (const FName& RN : RowNames)
                W.WriteLine(FString::Printf(TEXT("- `%s`"), *RN.ToString()));
        }

        FBPeekUsedBy::Write(W, Refs, AssetPath, KnownNormalized);
    }
};