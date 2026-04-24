#pragma once

#include "CoreMinimal.h"
#include "BPeekExtensionAPI.h"
#include "BPeekMarkdownWriter.h"
#include "BPeekTextUnwrap.h"
#include "BPeekAssetLinks.h"
#include "BPeekAssetPathHelpers.h"
#include "BPeekUsedBy.h"
#include "BPeekIndexBuilder.h"

#include "Engine/Blueprint.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "AttributeSet.h"
#include "GameplayTagContainer.h"
#include "UObject/UnrealType.h"

//
// Rich markdown for Gameplay Ability System assets:
//   - UGameplayAbility, UGameplayEffect, UAttributeSet (C++ classes, or
//     their UBlueprint BP subclasses like GA_Jump, GE_Damage).
//
// Detection works in two directions to cover both shapes:
//   - UBlueprint assets where ParentClass descends from one of the three
//     GAS roots — this is the common case ("GA_Foo", "GE_Damage" etc).
//   - Direct CDOs of C++-only GAS subclasses (UAttributeSet is almost
//     always C++-only; abilities occasionally are too).
//
// CanHandle runs before Blueprint extension's (priority 200 vs 100), so
// GA_*/GE_*/AS_* BPs land here instead of the generic Blueprint path.
//
// Without this extension, BP-derived GAS assets fall back to the
// Blueprint writer (~60% info coverage — parent class + vars, but no
// ability-tag/cooldown/modifier table). Not catastrophic, just less
// useful for AI consumers.
//
class FBPeekGASExtension : public IBPeekExtension
{
public:
    FName   GetId()          const override { return TEXT("bpeek.gas"); }
    FString GetVersionName() const override { return TEXT("1.0.0"); }
    int32   GetPriority()    const override { return 200; }

    TArray<UClass*> GetHandledClasses() const override
    {
        return {
            UBlueprint::StaticClass(),
            UGameplayAbility::StaticClass(),
            UGameplayEffect::StaticClass(),
            UAttributeSet::StaticClass()
        };
    }

    bool CanHandle(UObject* Asset) const override
    {
        if (!Asset) return false;
        if (Asset->HasAnyFlags(RF_ClassDefaultObject)) return false;

        // UBlueprint path — look at the parent chain.
        if (UBlueprint* BP = Cast<UBlueprint>(Asset))
        {
            for (UClass* P = BP->ParentClass; P; P = P->GetSuperClass())
            {
                if (P == UGameplayAbility::StaticClass()) return true;
                if (P == UGameplayEffect::StaticClass())  return true;
                if (P == UAttributeSet::StaticClass())    return true;
            }
            return false;
        }

        // Non-Blueprint native instances — rare in editor scans (mostly
        // runtime), but CDOs would already be filtered above.
        return Cast<UGameplayAbility>(Asset) != nullptr
            || Cast<UGameplayEffect>(Asset)  != nullptr
            || Cast<UAttributeSet>(Asset)    != nullptr;
    }

    void Write(FBPeekMarkdownWriter& W, UObject* Asset, const FBPeekScanContext& Ctx) override
    {
        if (!Asset) return;
        const FString AssetPath = Asset->GetPathName();

        // Determine the GAS flavour to label the heading correctly.
        const TCHAR* Flavour = ResolveFlavour(Asset);
        const FString DisplayPath = Ctx.bVerboseMode ? AssetPath : FBPeekAssetPath::Compact(AssetPath);

        W.WriteHeading(1, FString::Printf(TEXT("%s (%s)"), *Asset->GetName(), Flavour));
        W.WriteLine();
        W.WriteMetaRowCode(TEXT("Asset path"), DisplayPath);

        // Resolve the class we iterate properties on. For a UBlueprint
        // the BP's own UPROPERTY set is near-empty — the authored data
        // lives on the GeneratedClass CDO. Render against that instead.
        UObject* Subject = Asset;
        UClass* SubjectClass = Asset->GetClass();
        if (UBlueprint* BP = Cast<UBlueprint>(Asset))
        {
            if (BP->GeneratedClass)
            {
                SubjectClass = BP->GeneratedClass;
                Subject = BP->GeneratedClass->GetDefaultObject();
                W.WriteMetaRowCode(TEXT("Parent class"),
                    BP->ParentClass ? BP->ParentClass->GetName() : TEXT("<none>"));
            }
        }
        W.WriteMetaRowCode(TEXT("Class"), SubjectClass->GetName());
        W.WriteLine();

        if (!Subject)
        {
            W.WriteLine(TEXT("_No CDO available to inspect properties._"));
            return;
        }

        // Same property-extraction rules as DataAssetWriter — honour
        // CPF_Edit, skip transient and bOverride_ gates.
        struct FProp { FString Name; FString Value; };
        TArray<FProp> Props;
        for (TFieldIterator<FProperty> It(SubjectClass); It; ++It)
        {
            FProperty* P = *It;
            if (!P->HasAnyPropertyFlags(CPF_Edit)) continue;
            if (P->HasAnyPropertyFlags(CPF_Transient)) continue;
            // Filter out noise from UObject/UGameplayAbility base scaffolding
            // that isn't informative at the MD level. We keep properties
            // declared on GAS roots because cooldown/cost/tags live there.
            UClass* Owner = P->GetOwnerClass();
            if (Owner == UObject::StaticClass()) continue;

            const FString PropName = P->GetName();
            if (PropName.StartsWith(TEXT("bOverride_")))
            {
                if (FBoolProperty* BP = CastField<FBoolProperty>(P);
                    BP && !BP->GetPropertyValue_InContainer(Subject)) continue;
            }
            FString Val;
            if (FObjectProperty* OP = CastField<FObjectProperty>(P))
            {
                UObject* V = OP->GetObjectPropertyValue_InContainer(Subject);
                if (V) Val = V->GetPathName();
            }
            else if (FSoftObjectProperty* SP = CastField<FSoftObjectProperty>(P))
            {
                const FSoftObjectPtr V = SP->GetPropertyValue_InContainer(Subject);
                if (!V.IsNull()) Val = V.ToString();
            }
            else if (FTextProperty* TP = CastField<FTextProperty>(P))
            {
                const FText V = TP->GetPropertyValue_InContainer(Subject);
                if (!V.IsEmpty()) Val = V.ToString();
            }
            else
            {
                P->ExportTextItem_Direct(Val, P->ContainerPtrToValuePtr<void>(Subject),
                                         nullptr, nullptr, PPF_None);
                if (Val == TEXT("None") || Val == TEXT("\"\"") || Val == TEXT("()"))
                    Val = FString();
            }
            if (Val.IsEmpty()) continue;
            Props.Add({ PropName, Val });
        }

        static const TMap<FString, TArray<FString>> EmptyRefs;
        static const TSet<FString> EmptyKnown;
        const TSet<FString>& Known = Ctx.Known ? *Ctx.Known : EmptyKnown;

        if (Props.Num() == 0)
        {
            W.WriteLine(TEXT("_No editable GAS properties._"));
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
                    FString V = FBPeekTextUnwrap::Unwrap(P.Value);
                    V = FBPeekAssetLinks::Linkify(V, AssetPath, Known);
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
                    V = FBPeekAssetLinks::Linkify(V, AssetPath, Known);
                    V.ReplaceInline(TEXT(","), TEXT(";"));
                    Parts.Add(FString::Printf(TEXT("`%s`=%s"), *P.Name, *V));
                }
                W.WriteLine(FString::Printf(TEXT("**Properties (%d):** %s"),
                    Props.Num(), *FString::Join(Parts, TEXT(", "))));
                W.WriteLine();
            }
        }

        FBPeekUsedBy::Write(W,
            Ctx.Refs ? *Ctx.Refs : EmptyRefs,
            AssetPath, Known);
    }

    void AppendToIndex(FBPeekIndexBuilder& IB, UObject* Asset) const override
    {
        // Current IndexBuilder buckets are fixed per-type. Route BP
        // subclasses through AddBlueprint so they still show up in the
        // index; a dedicated "Gameplay Abilities" section is a future
        // improvement (needs a new bucket on IndexBuilder).
        if (UBlueprint* BP = Cast<UBlueprint>(Asset)) IB.AddBlueprint(BP);
    }

private:
    static const TCHAR* ResolveFlavour(UObject* Asset)
    {
        // Walk the parent chain once to pick the correct label. Order
        // matters: AttributeSet/Effect/Ability are siblings, so we
        // check each explicitly.
        UClass* Start = nullptr;
        if (UBlueprint* BP = Cast<UBlueprint>(Asset))
            Start = BP->ParentClass;
        else
            Start = Asset->GetClass();

        for (UClass* P = Start; P; P = P->GetSuperClass())
        {
            if (P == UGameplayAbility::StaticClass()) return TEXT("Gameplay Ability");
            if (P == UGameplayEffect::StaticClass())  return TEXT("Gameplay Effect");
            if (P == UAttributeSet::StaticClass())    return TEXT("Attribute Set");
        }
        return TEXT("Gameplay Ability System");
    }
};
