// BPeekBehaviorTree — rich markdown for BehaviorTree / BlackboardData
// assets and their BP subclasses (Tasks / Decorators / Services).
// Without this module's extension registering, BT/BB assets fall
// through to the generic UObject dispatch and get nothing specialised.
//
// AIModule is a built-in engine plugin
// (Engine/Plugins/Runtime/AIModule). Detection below is filesystem-based
// so a stripped engine install — or BPEEK_RELEASE_BUILD opt-out — produces
// an empty module with no link errors and no runtime extension registration.
// Pattern mirrored from the other optional-dep submodules (GAS / Flow / EI).

using UnrealBuildTool;
using System.IO;

public class BPeekBehaviorTree : BPeekBuild
{
    public BPeekBehaviorTree(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "BPeek", "BPeekCompat"
        });

        // AIModule is an engine built-in module — always included in
        // release builds. Detection only skips linkage on stripped
        // engine installs.
        bool bHasAI = false;
        {
            string EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
            string[] SearchPaths = new[] {
                Path.Combine(EngineDir, "Plugins", "Runtime", "AIModule"),
                Path.Combine(EngineDir, "Plugins", "AIModule"),
            };
            foreach (string P in SearchPaths)
            {
                if (Directory.Exists(P)) { bHasAI = true; break; }
            }
            // AIModule also ships as a core engine module (Engine/Source/Runtime/AIModule)
            // depending on build config — fall back to checking that path.
            if (!bHasAI)
            {
                string AIRuntime = Path.Combine(EngineDir, "Source", "Runtime", "AIModule");
                if (Directory.Exists(AIRuntime)) bHasAI = true;
            }
        }

        if (bHasAI)
        {
            PublicDependencyModuleNames.AddRange(new string[] {
                "AIModule",
                "GameplayTasks",  // UBTTask_RunBehaviorDynamic and related
                // UMG/UMGEditor — AppendToIndex for BT-node BPs calls
                // AddBlueprint, which widget-counts via UWidgetBlueprint.
                "UMG",
                "UMGEditor"
            });
            PublicDefinitions.Add("BPEEK_WITH_BEHAVIORTREE=1");
        }
        else
        {
            PublicDefinitions.Add("BPEEK_WITH_BEHAVIORTREE=0");
        }
    }
}
