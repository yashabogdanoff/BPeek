#pragma once
#include "CoreMinimal.h"
#include "BPeekMarkdownWriter.h"
#include "BPeekTextUnwrap.h"
#include "BPeekAssetLinks.h"
#include "BPeekAssetPathHelpers.h"
#include "BPeekUsedBy.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"

class FBPeekLevelWriter
{
public:
    static void Write(FBPeekMarkdownWriter& W, UWorld* World,
                      const TMap<FString, TArray<FString>>& Refs = TMap<FString, TArray<FString>>(),
                      const TSet<FString>& KnownNormalized = TSet<FString>(),
                      bool bVerboseMode = false)
    {
        if (!World || !World->PersistentLevel) return;
        const FString AssetPath = World->GetPathName();
        // Plain path-and-dot split — don't use FBPeekAssetPath::ShortName
        // here: it strips trailing `_C` (for BP-generated classes) which
        // would turn "L_Office_C" into "L_Office". Level asset names are
        // kept as-is to match C# LevelDefinition.Name.
        FString Name = AssetPath;
        int32 Slash; if (Name.FindLastChar(TEXT('/'), Slash)) Name = Name.Mid(Slash + 1);
        int32 Dot; if (Name.FindChar(TEXT('.'), Dot)) Name = Name.Left(Dot);

        // Pass 1: collect actor descriptors with filtered props.
        struct FActorInfo
        {
            FString Name; FString ClassName;
            FString Location; FString Rotation; FString Scale;
            TArray<TPair<FString, FString>> Properties;
        };
        TArray<FActorInfo> Actors;
        for (AActor* A : World->PersistentLevel->Actors)
        {
            if (!A) continue;
            FActorInfo AI;
            AI.Name = A->GetName();
            AI.ClassName = A->GetClass()->GetPathName();
            const FTransform T = A->GetActorTransform();
            AI.Location = T.GetLocation().ToString();
            AI.Rotation = T.GetRotation().Rotator().ToString();
            AI.Scale = T.GetScale3D().ToString();

            const bool bIsEngineStaticDecor =
                A->GetClass()->GetPathName().StartsWith(TEXT("/Script/Engine.")) &&
                A->IsA(AStaticMeshActor::StaticClass());

            if (!bIsEngineStaticDecor)
            {
                for (TFieldIterator<FProperty> PIt(A->GetClass()); PIt; ++PIt)
                {
                    FProperty* P = *PIt;
                    if (!P->HasAnyPropertyFlags(CPF_Edit)) continue;
                    if (P->HasAnyPropertyFlags(CPF_Transient)) continue;
                    if (P->GetOwnerClass() == AActor::StaticClass()) continue;
                    const FString PropName = P->GetName();
                    if (PropName.StartsWith(TEXT("bOverride_")))
                    {
                        if (FBoolProperty* BP = CastField<FBoolProperty>(P);
                            BP && !BP->GetPropertyValue_InContainer(A)) continue;
                    }
                    FString Val;
                    if (FObjectProperty* OP = CastField<FObjectProperty>(P))
                    {
                        UObject* V = OP->GetObjectPropertyValue_InContainer(A);
                        if (V) Val = V->GetPathName();
                    }
                    else if (FSoftObjectProperty* SP = CastField<FSoftObjectProperty>(P))
                    {
                        const FSoftObjectPtr V = SP->GetPropertyValue_InContainer(A);
                        if (!V.IsNull()) Val = V.ToString();
                    }
                    else if (FTextProperty* TP = CastField<FTextProperty>(P))
                    {
                        const FText V = TP->GetPropertyValue_InContainer(A);
                        if (!V.IsEmpty()) Val = V.ToString();
                    }
                    else
                    {
                        P->ExportTextItem_Direct(Val, P->ContainerPtrToValuePtr<void>(A), nullptr, nullptr, PPF_None);
                        if (Val == TEXT("None") || Val == TEXT("\"\"") || Val == TEXT("()")) Val = FString();
                    }
                    if (!Val.IsEmpty()) AI.Properties.Add({ PropName, Val });
                }
            }
            Actors.Add(MoveTemp(AI));
        }

        const FString DisplayPath = bVerboseMode ? AssetPath : FBPeekAssetPath::Compact(AssetPath);
        W.WriteHeading(1, FString::Printf(TEXT("%s (Level)"), *Name));
        W.WriteLine();
        W.WriteMetaRowCode(TEXT("Asset path"), DisplayPath);
        W.WriteMetaRow(TEXT("Actors"), FString::FromInt(Actors.Num()));
        W.WriteLine();

        if (Actors.Num() == 0)
        {
            FBPeekUsedBy::Write(W, Refs, AssetPath, KnownNormalized);
            return;
        }

        // Actor types table — grouped by class, descending count, insertion
        // order as tiebreak (mimics LINQ GroupBy stable ordering).
        TArray<TPair<FString, int32>> ByClass;
        {
            TMap<FString, int32> Counts;
            TArray<FString> Order;  // first-seen class order
            for (const FActorInfo& A : Actors)
            {
                int32* Existing = Counts.Find(A.ClassName);
                if (Existing) { ++(*Existing); }
                else { Counts.Add(A.ClassName, 1); Order.Add(A.ClassName); }
            }
            for (const FString& C : Order) ByClass.Add({ C, Counts[C] });
            // Sort descending by count, stable on first-seen order (fallback
            // index = original position in Order).
            TMap<FString, int32> OrderIdx;
            for (int32 i = 0; i < Order.Num(); ++i) OrderIdx.Add(Order[i], i);
            ByClass.Sort([&OrderIdx](const TPair<FString,int32>& A, const TPair<FString,int32>& B){
                if (A.Value != B.Value) return A.Value > B.Value;
                return OrderIdx[A.Key] < OrderIdx[B.Key];
            });
        }

        W.WriteHeading(2, TEXT("Actor types"));
        W.WriteLine();
        W.WriteLine(TEXT("| Class | Count |"));
        W.WriteLine(TEXT("|---|---:|"));
        for (const auto& KV : ByClass)
        {
            const FString Cls = FBPeekAssetLinks::Linkify(KV.Key, AssetPath, KnownNormalized);
            W.WriteLine(FString::Printf(TEXT("| %s | %d |"), *Cls, KV.Value));
        }
        W.WriteLine();

        // Split into configured / decor buckets. Copy rather than point into
        // Actors — UE's TArray::Sort on pointer element types doesn't play
        // well with nested-struct lambdas.
        TArray<FActorInfo> WithProps, NoProps;
        for (const FActorInfo& A : Actors)
        {
            if (A.Properties.Num() > 0) WithProps.Add(A);
            else NoProps.Add(A);
        }
        // Pure ordinal case-sensitive compare — matches C# .OrderBy default
        // (StringComparer.CurrentCulture / invariant), where `_` (0x5F) sorts
        // below lowercase letters. OrdinalIgnoreCase (used for Used-by) would
        // upper-fold and flip `SM_Pen_…` vs `SM_PencilCase_…` the wrong way.
        auto ActorSort = [](const FActorInfo& A, const FActorInfo& B){
            const int32 C = A.ClassName.Compare(B.ClassName, ESearchCase::CaseSensitive);
            if (C != 0) return C < 0;
            return A.Name.Compare(B.Name, ESearchCase::CaseSensitive) < 0;
        };
        if (WithProps.Num() > 0)
        {
            WithProps.Sort(ActorSort);
            W.WriteHeading(2, TEXT("Actors with configuration"));
            W.WriteLine();
            for (const FActorInfo& A : WithProps)
            {
                W.WriteHeading(3, A.Name);
                const FString Cls = FBPeekAssetLinks::Linkify(A.ClassName, AssetPath, KnownNormalized);
                W.WriteLine(FString::Printf(TEXT("- Class: %s"), *Cls));
                if (!A.Location.IsEmpty()) W.WriteLine(FString::Printf(TEXT("- Location: `%s`"), *A.Location));
                if (!A.Rotation.IsEmpty()) W.WriteLine(FString::Printf(TEXT("- Rotation: `%s`"), *A.Rotation));
                if (!A.Scale.IsEmpty() && A.Scale != TEXT("X=1.000 Y=1.000 Z=1.000"))
                    W.WriteLine(FString::Printf(TEXT("- Scale: `%s`"), *A.Scale));
                TArray<TPair<FString,FString>> SortedProps = A.Properties;
                SortedProps.Sort([](const TPair<FString,FString>& X, const TPair<FString,FString>& Y){
                    return X.Key.Compare(Y.Key, ESearchCase::CaseSensitive) < 0;
                });
                for (const auto& KV : SortedProps)
                {
                    FString V = FBPeekTextUnwrap::Unwrap(KV.Value);
                    V = FBPeekAssetLinks::Linkify(V, AssetPath, KnownNormalized);
                    W.WriteLine(FString::Printf(TEXT("- %s: %s"), *KV.Key, *V));
                }
                W.WriteLine();
            }
        }

        if (NoProps.Num() > 0)
        {
            NoProps.Sort(ActorSort);
            if (bVerboseMode)
            {
                W.WriteHeading(2, FString::Printf(TEXT("Decor / static actors (%d)"), NoProps.Num()));
                W.WriteLine();
                W.WriteLine(TEXT("| Name | Class | Location |"));
                W.WriteLine(TEXT("|---|---|---|"));
                for (const FActorInfo& A : NoProps)
                {
                    const FString Cls = FBPeekAssetLinks::Linkify(A.ClassName, AssetPath, KnownNormalized);
                    W.WriteLine(FString::Printf(TEXT("| `%s` | %s | `%s` |"), *A.Name, *Cls, *A.Location));
                }
                W.WriteLine();
            }
            else
            {
                // Default (AI-optimised): skip the decor table entirely.
                // Every row here is a configuration-free instance of a
                // class already counted in the "Actor types" table above.
                // Listing 6000 identical StaticMeshActors burns tokens for
                // no information gain. One summary line keeps the AI
                // honest about what was dropped.
                W.WriteLine(FString::Printf(
                    TEXT("*%d decor / static actors with default transforms "
                         "suppressed (aggregate counts in Actor types above; "
                         "full listing available with --verbose).*"),
                    NoProps.Num()));
                W.WriteLine();
            }
        }

        FBPeekUsedBy::Write(W, Refs, AssetPath, KnownNormalized);
    }
};