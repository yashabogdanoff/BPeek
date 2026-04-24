// BPeekEnhancedInput — rich markdown for UInputMappingContext assets
// (key/action/modifiers tables). Without this module's extension
// registering, IMC assets fall through to the generic DataAsset dump
// from BPeek core.
//
// EnhancedInput is a built-in engine plugin (Engine/Plugins/EnhancedInput),
// present in every UE 5.x install — so the detection below almost always
// succeeds. The filesystem check is a safety net for stripped engine
// builds where someone removed the plugin, and for release builds where
// we opt out of all optional deps (see BPEEK_RELEASE_BUILD).
//
// When the plugin folder is missing, the module still compiles but as
// an empty shell — no hard dep on EnhancedInput, no extension
// registration at runtime.

using UnrealBuildTool;
using System.IO;

public class BPeekEnhancedInput : BPeekBuild
{
    public BPeekEnhancedInput(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "BPeek", "BPeekCompat"
        });

        // EnhancedInput is an engine built-in plugin — present in every
        // supported UE install. Detection still runs so release builds
        // on stripped engine installs don't hard-link against missing
        // symbols, but BPEEK_RELEASE_BUILD does NOT opt out: users
        // downloading the release zip expect IMC rendering to work.
        bool bHasEnhancedInput = false;
        {
            string EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
            string[] SearchPaths = new[] {
                Path.Combine(EngineDir, "Plugins", "EnhancedInput"),
                Path.Combine(EngineDir, "Plugins", "Runtime", "EnhancedInput"),
                Path.Combine(EngineDir, "Plugins", "Experimental", "EnhancedInput"),
            };
            foreach (string P in SearchPaths)
            {
                if (Directory.Exists(P)) { bHasEnhancedInput = true; break; }
            }
        }

        if (bHasEnhancedInput)
        {
            PrivateDependencyModuleNames.Add("EnhancedInput");
            PublicDefinitions.Add("BPEEK_WITH_ENHANCEDINPUT=1");
        }
        else
        {
            PublicDefinitions.Add("BPEEK_WITH_ENHANCEDINPUT=0");
        }
    }
}
