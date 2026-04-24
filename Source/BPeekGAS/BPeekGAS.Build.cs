// BPeekGAS — rich markdown for Gameplay Ability System assets
// (UGameplayAbility / UGameplayEffect / UAttributeSet and their BP
// subclasses). Without this module registering, GA_*/GE_*/AS_* BPs fall
// through to the generic Blueprint writer (~60% info coverage — parent
// class + vars, but no ability-tag/cooldown/modifier table).
//
// GameplayAbilities is a built-in engine plugin
// (Engine/Plugins/Runtime/GameplayAbilities). Detection below is
// filesystem-based so a stripped engine install produces an empty
// module with no link errors and no runtime extension registration.

using UnrealBuildTool;
using System.IO;

public class BPeekGAS : BPeekBuild
{
    public BPeekGAS(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "BPeek", "BPeekCompat"
        });

        // GameplayAbilities is an engine built-in plugin — always included
        // in release builds. Detection only skips linkage on stripped
        // engine installs.
        bool bHasGAS = false;
        {
            string EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
            string[] SearchPaths = new[] {
                Path.Combine(EngineDir, "Plugins", "Runtime", "GameplayAbilities"),
                Path.Combine(EngineDir, "Plugins", "GameplayAbilities"),
            };
            foreach (string P in SearchPaths)
            {
                if (Directory.Exists(P)) { bHasGAS = true; break; }
            }
        }

        if (bHasGAS)
        {
            PublicDependencyModuleNames.AddRange(new string[] {
                "GameplayAbilities",
                "GameplayTags",
                "GameplayTasks",
                // UMG/UMGEditor needed transitively because our extension's
                // AppendToIndex path calls FBPeekIndexBuilder::AddBlueprint,
                // which widget-counts via UWidgetBlueprint reflection.
                "UMG",
                "UMGEditor"
            });
            PublicDefinitions.Add("BPEEK_WITH_GAS=1");
        }
        else
        {
            PublicDefinitions.Add("BPEEK_WITH_GAS=0");
        }
    }
}
