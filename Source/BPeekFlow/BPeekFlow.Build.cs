// BPeekFlow — rich markdown for the community Flow plugin (UFlowAsset
// and UFlowNodeBlueprint). Without this module registering, .flow
// assets fall through to the generic UObject dispatch and get nothing
// specialised.
//
// Flow is NOT built into the engine — it's a marketplace/community
// plugin (Moth Cubes). Typical install locations are:
//   - Engine/Plugins/Marketplace/Flow/   ← Fab / marketplace install
//   - Engine/Plugins/Flow/               ← manual drop into engine
//   - <Project>/Plugins/Flow/            ← per-project install
//   - <Project>/Plugins/GameFeatures/Flow/
//
// Detection tries all four. When BuildPlugin runs for a release zip,
// we want BPEEK_WITH_FLOW=1 if Flow is installed in any of those
// locations on the developer machine — the resulting binary will hard-
// link against the Flow module. End users who DON'T have Flow get
// gracefully skipped thanks to `"Flow": { "Optional": true }` in
// BPeek.uplugin: UE drops the BPeekFlow module at mount time but the
// rest of BPeek loads normally.
//
// Release binaries support Flow only when the build machine has the
// Flow plugin installed (filesystem detection populates WITH_FLOW=1
// at compile time). End users who need Flow rendering clone the
// source and build against their local install — see Docs/.

using UnrealBuildTool;
using System.IO;

public class BPeekFlow : BPeekBuild
{
    public BPeekFlow(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "BPeek", "BPeekCompat"
        });

        // BPEEK_RELEASE_BUILD=1 forces Flow integration off — used for
        // public release zips so the resulting BPeekFlow.dll has no
        // hard imports from the Flow plugin and loads fine on hosts
        // where Flow isn't enabled (Lyra, Cropout, …). Source-install
        // users skip the env var and get full Flow rendering when
        // their project / engine has the plugin installed.
        bool bReleaseBuild = System.Environment.GetEnvironmentVariable("BPEEK_RELEASE_BUILD") == "1";
        if (bReleaseBuild)
        {
            PublicDefinitions.Add("BPEEK_WITH_FLOW=0");
            return;
        }

        bool bHasFlow = false;

        // Engine-level locations — work for both project and standalone
        // plugin builds (Target.ProjectFile may be null in the latter).
        {
            string EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
            string[] EngineSearchPaths = new[] {
                Path.Combine(EngineDir, "Plugins", "Marketplace", "Flow"),
                Path.Combine(EngineDir, "Plugins", "Flow"),
            };
            foreach (string P in EngineSearchPaths)
            {
                if (Directory.Exists(P)) { bHasFlow = true; break; }
            }
        }

        // Project-level locations — only meaningful when building against
        // a specific project (deploy-and-run workflow).
        if (!bHasFlow && Target.ProjectFile != null)
        {
            string ProjectDir = Path.GetDirectoryName(Target.ProjectFile.FullName);
            string[] ProjectSearchPaths = new[] {
                Path.Combine(ProjectDir, "Plugins", "Flow"),
                Path.Combine(ProjectDir, "Plugins", "GameFeatures", "Flow"),
            };
            foreach (string P in ProjectSearchPaths)
            {
                if (Directory.Exists(P)) { bHasFlow = true; break; }
            }
        }

        if (bHasFlow)
        {
            PublicDependencyModuleNames.AddRange(new string[] {
                "Flow",
                // UMG/UMGEditor — AppendToIndex for FlowNode BPs calls
                // AddBlueprint, which widget-counts via UWidgetBlueprint.
                "UMG",
                "UMGEditor"
            });
            PublicDefinitions.Add("BPEEK_WITH_FLOW=1");
        }
        else
        {
            PublicDefinitions.Add("BPEEK_WITH_FLOW=0");
        }
    }
}
