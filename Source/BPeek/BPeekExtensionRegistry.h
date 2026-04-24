#pragma once

#include "CoreMinimal.h"
#include "BPeekExtensionAPI.h"

//
// Thin sugar over IModularFeatures for the common queries we need.
// Everything is static — there's no Registry instance, just helpers
// that read the IModularFeatures registry by feature name.
//
// Extensions register themselves in their module's StartupModule with
// IModularFeatures::Get().RegisterModularFeature(...). See
// Tests/BPeekExtensionRegistryTests.cpp for a working example.
//

class BPEEK_API FBPeekExtensionRegistry
{
public:
    /** All currently-registered extensions, sorted by GetPriority()
     *  descending (highest priority first). Stable ordering among equal
     *  priorities: insertion order from IModularFeatures. */
    static TArray<IBPeekExtension*> GetAll();

    /** Highest-priority extension whose CanHandle(Asset) returns true,
     *  or nullptr if nothing claims the asset. */
    static IBPeekExtension* FindFor(UObject* Asset);

    /** True if at least one extension declares Cls (or one of its
     *  parents) in GetHandledClasses(). Used by scanner to short-circuit
     *  TObjectIterator on classes nobody cares about. */
    static bool IsClassHandled(const UClass* Cls);

    /** Compatibility gate — returns false if extension's GetAPIVersion
     *  is incompatible with the core build. Core skips dispatch to
     *  incompatible extensions and logs a warning. */
    static bool IsAPICompatible(const IBPeekExtension* Ext);
};
