#pragma once

#include "CoreMinimal.h"
#include "BPeekSemver.h"

class IPlugin;

/**
 * Declarations parsed out of the `BPeek` object in an extension's
 * .uplugin file. Example:
 *
 *   "BPeek": {
 *     "CoreVersionMin":   "1.0.0",
 *     "CoreVersionMax":   "2.0.0",
 *     "TargetPlugin":     "Flow",
 *     "TargetVersionMin": "3.0.0",
 *     "TargetVersionMax": ""
 *   }
 *
 * Empty version strings mean "any" on that side of the range. If
 * TargetPlugin is empty the target check is skipped (useful for
 * extensions that don't wrap a third-party plugin — just core).
 */
struct BPEEK_API FBPeekExtensionManifest
{
    /** Plugin name as reported by IPlugin::GetName — used in logs. */
    FString PluginName;

    /** True if the .uplugin actually had a "BPeek" object. False means
     *  "not a BPeek extension" (e.g. random UE built-in plugins). */
    bool bPresent = false;

    FString CoreVersionMin;   // empty = no lower bound
    FString CoreVersionMax;   // empty = no upper bound; exclusive
    FString TargetPlugin;     // empty = no target check
    FString TargetVersionMin; // empty = no lower bound
    FString TargetVersionMax; // empty = no upper bound; exclusive
};

/** Outcome of a compatibility check — either OK or a human-readable
 *  reason the extension was rejected. */
struct BPEEK_API FBPeekCompatResult
{
    bool bCompatible = true;
    FString Reason;           // filled when !bCompatible
};

/**
 * Startup-time version gate for BPeek extensions. Iterates enabled
 * plugins, parses the `BPeek` field out of each .uplugin, compares
 * against installed core + target-plugin versions, logs warnings for
 * mismatches.
 *
 * Called once from FBPeekModule::StartupModule. Doesn't actually
 * *disable* incompatible extensions — IModularFeatures registration
 * happens inside the extension's own StartupModule, and by the time we
 * run this the extension may have already registered. What it does is
 * loudly flag the mismatch in the log so the user knows to upgrade or
 * downgrade.
 */
class BPEEK_API FBPeekVersionCheck
{
public:
    /** Parse the "BPeek" object out of an extension's .uplugin. Returns
     *  a manifest with bPresent=false when the plugin has no such
     *  object (i.e. it isn't a BPeek extension). */
    static FBPeekExtensionManifest LoadManifest(const IPlugin& Plugin);

    /** Pure check — given a manifest and the versions to compare
     *  against, decide if the extension is compatible. Exposed for
     *  unit tests; production path goes through RunStartupCheck. */
    static FBPeekCompatResult CheckCompat(
        const FBPeekExtensionManifest& Manifest,
        const FBPeekSemver& InstalledCoreVersion,
        const FBPeekSemver* InstalledTargetVersion);

    /** Iterate enabled plugins, load+check each BPeek-tagged manifest,
     *  log results. Safe to call before any extension StartupModule
     *  runs — we only read descriptors, not extension state. */
    static void RunStartupCheck();
};
