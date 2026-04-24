// BPeek build hub — one place for all UE-version PublicDefinitions and
// build-time settings. Every other .Build.cs in this plugin (and in
// BPeek extension plugins) should inherit from this class instead of
// ModuleRules directly, so the BPEEK_UE_*_OR_LATER macros reach every
// module automatically.
//
// Pattern stolen from Redpoint EOS (RedpointEOSBuild). Avoids scattering
// `#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= N` across
// every .cpp file.
//
// NOT registered as a module in BPeek.uplugin — this is an abstract
// base class only. UBT discovers it because .Build.cs files are scanned
// recursively under Source/.

using UnrealBuildTool;

public abstract class BPeekBuild : ModuleRules
{
    protected BPeekBuild(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Minimum supported engine version for BPeek is 5.4 — asserted
        // as an always-1 macro so downstream code can rely on it when
        // writing forward-compat code.
        PublicDefinitions.Add("BPEEK_UE_5_4_OR_LATER=1");

#if UE_5_5_OR_LATER
        PublicDefinitions.Add("BPEEK_UE_5_5_OR_LATER=1");
#else
        PublicDefinitions.Add("BPEEK_UE_5_5_OR_LATER=0");
#endif

#if UE_5_6_OR_LATER
        PublicDefinitions.Add("BPEEK_UE_5_6_OR_LATER=1");
#else
        PublicDefinitions.Add("BPEEK_UE_5_6_OR_LATER=0");
#endif

#if UE_5_7_OR_LATER
        PublicDefinitions.Add("BPEEK_UE_5_7_OR_LATER=1");
#else
        PublicDefinitions.Add("BPEEK_UE_5_7_OR_LATER=0");
#endif

#if UE_5_8_OR_LATER
        PublicDefinitions.Add("BPEEK_UE_5_8_OR_LATER=1");
#else
        PublicDefinitions.Add("BPEEK_UE_5_8_OR_LATER=0");
#endif
    }
}
