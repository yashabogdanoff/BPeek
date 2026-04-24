#pragma once
#include "CoreMinimal.h"
#include "BPeekMarkdownWriter.h"
#include "Engine/Blueprint.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimStateMachineTypes.h"

/**
 * Coverage counters for AnimBP state machines. Aggregated into
 * FBPeekCoverageStats in BPeekGraphWalker.h via operator+=.
 */
struct FBPeekAnimStats
{
    int32 AnimBlueprints    = 0;  // # of UAnimBlueprints seen
    int32 StateMachines     = 0;  // sum of BakedStateMachines.Num()
    int32 States            = 0;  // sum of states across all state machines
    int32 Transitions       = 0;  // sum of transitions across all state machines
};

/**
 * Renders `## State Machines (N)` section for UAnimBlueprint instances.
 * Pulls data from the compiled UAnimBlueprintGeneratedClass::BakedStateMachines
 * — safe to read without loading editor-only state machine UEdGraph.
 *
 * For each state machine: list of states (with initial / conduit markers)
 * and list of transitions (from → to with crossfade duration). Transition
 * rules themselves are not analyzed — each rule lives in a sub-graph with
 * its own exec chain, out of scope for baseline AnimBP coverage.
 */
class FBPeekAnimStateMachineWriter
{
public:
    static FBPeekAnimStats Write(FBPeekMarkdownWriter& W, UBlueprint* BP)
    {
        FBPeekAnimStats Stats;
#if WITH_EDITOR
        UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(BP);
        if (!AnimBP) return Stats;
        Stats.AnimBlueprints = 1;

        UAnimBlueprintGeneratedClass* GenClass =
            Cast<UAnimBlueprintGeneratedClass>(AnimBP->GeneratedClass);
        if (!GenClass || GenClass->BakedStateMachines.Num() == 0) return Stats;

        W.WriteHeading(2, FString::Printf(TEXT("State Machines (%d)"),
            GenClass->BakedStateMachines.Num()));
        W.WriteLine();

        for (const FBakedAnimationStateMachine& SM : GenClass->BakedStateMachines)
        {
            Stats.StateMachines++;
            Stats.States       += SM.States.Num();
            Stats.Transitions  += SM.Transitions.Num();

            W.WriteHeading(3, SM.MachineName.ToString());

            // States
            if (SM.States.Num() > 0)
            {
                W.WriteLine(TEXT("**States:**"));
                for (int32 i = 0; i < SM.States.Num(); ++i)
                {
                    const FBakedAnimationState& S = SM.States[i];
                    FString InitialMarker = (i == SM.InitialState)
                        ? FString(TEXT(" _(initial)_")) : FString();
                    FString ConduitMarker = S.bIsAConduit
                        ? FString(TEXT(" _(conduit)_")) : FString();
                    W.WriteLine(FString::Printf(TEXT("- `%s`%s%s"),
                        *S.StateName.ToString(), *InitialMarker, *ConduitMarker));
                }
                W.WriteLine();
            }

            // Transitions
            if (SM.Transitions.Num() > 0)
            {
                W.WriteLine(TEXT("**Transitions:**"));
                for (const FAnimationTransitionBetweenStates& T : SM.Transitions)
                {
                    FString FromName = SM.States.IsValidIndex(T.PreviousState)
                        ? SM.States[T.PreviousState].StateName.ToString() : TEXT("?");
                    FString ToName = SM.States.IsValidIndex(T.NextState)
                        ? SM.States[T.NextState].StateName.ToString() : TEXT("?");
                    W.WriteLine(FString::Printf(TEXT("- `%s` → `%s` (blend: %.2fs)"),
                        *FromName, *ToName, T.CrossfadeDuration));
                }
                W.WriteLine();
            }
        }
#endif
        return Stats;
    }
};
