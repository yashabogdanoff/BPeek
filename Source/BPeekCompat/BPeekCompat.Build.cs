// BPeekCompat — thin polyfill module for engine APIs that differ between
// supported UE versions. Start empty; add helpers here whenever a UE
// bump forces divergent call sites (pattern: Redpoint EOS's
// RedpointEOSCompat).
//
// Registered as a real module in BPeek.uplugin (Editor / Default) so
// every other BPeek module can PublicDependencyModuleNames it.

using UnrealBuildTool;

public class BPeekCompat : BPeekBuild
{
    public BPeekCompat(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicIncludePaths.Add(ModuleDirectory);

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine"
        });
    }
}
