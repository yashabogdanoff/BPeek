#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"

class FBPeekMarkdownWriter;
class FBPeekIndexBuilder;
struct FBPeekCoverageStats;

//
// Extension API — plug-in contract that core writers AND third-party
// extensions (BPeekFlow, BPeekGAS, BPeekEnhancedInput, ...) both
// implement. Distribution happens through UE's IModularFeatures; no
// custom singleton.
//
// Breaking changes to this header MUST bump BPEEK_EXTENSION_API_VERSION
// and also bump BPeek core's MAJOR semver.
//

#define BPEEK_EXTENSION_API_VERSION 1

/** One row in the auto-generated _index.md. Extensions may customise
 *  via IBPeekExtension::AppendIndexEntry. */
struct FBPeekIndexEntry
{
    FString AssetPath;           // "/Game/X/Y.Y"
    FString DisplayName;         // human-readable
    FString RelativeMdPath;      // e.g. "Module_01/BP_Foo.md"
    FString Summary;             // one-liner shown next to the link, optional
    FName   Section = NAME_None; // grouping section in _index.md (optional)
};

/** Per-asset context handed to IBPeekExtension::Write. Mutable shell
 *  around the shared-for-scan state (Refs, Known) and the per-asset
 *  identity (path, uasset). Core owns the lifetime, extensions only
 *  read. */
struct FBPeekScanContext
{
    // Per-asset identity — caller populates before each Write.
    FString AssetPath;           // "/Game/X/Y.Y"
    FString UassetRel;           // filesystem-relative, e.g. "Content/X/Y.uasset"
    FString MdPath;              // absolute path the scanner will SaveTo after Write
    bool    bIsCooked = false;   // true when scanning cooked assets

    // Shared for the whole scan — pointers to the scanner-owned state.
    // Can be null in synthetic contexts (tests). Extensions must
    // tolerate null.
    const TMap<FString, TArray<FString>>* Refs = nullptr;
    const TSet<FString>*                  Known = nullptr;

    // Project-wide index builder. When non-null, the extension's
    // AppendToIndex is called after a successful MD write (or when an
    // asset is skipped via hashes but still valid). Null in tests.
    FBPeekIndexBuilder* IndexBuilder = nullptr;

    // Optional coverage-stats accumulator for extensions that report
    // per-asset stats (currently just Blueprint). Extensions should
    // only touch this when their own Write produces stats.
    FBPeekCoverageStats* CoverageOut = nullptr;

    // When true, writers should emit the AI-optimised compact layout
    // (one-line-per-item lists) instead of full markdown tables. The
    // Logic code-blocks and the Used-by section are rendered the same
    // either way — the switch only affects property/variable/row
    // enumerations where table syntax dominates the token count.
    bool bVerboseMode = false;
};

/**
 * Contract implemented by every BPeek writer (built-in and third-party).
 * Registered via IModularFeatures under IBPeekExtension::GetModularFeatureName()
 * at module StartupModule; unregistered at ShutdownModule.
 *
 * Resolution: scanner iterates GetHandledClasses(), runs TObjectIterator,
 * asks each extension (priority-sorted) CanHandle(asset). First extension
 * that says yes owns the asset.
 */
class BPEEK_API IBPeekExtension : public IModularFeature
{
public:
    static FName GetModularFeatureName()
    {
        static const FName Name(TEXT("BPeekExtension"));
        return Name;
    }

    virtual ~IBPeekExtension() = default;

    // --- Identity -------------------------------------------------------
    /** Stable ID, e.g. "bpeek.core.enum", "bpeek.gas". Used in logs and
     *  for deduplication. */
    virtual FName GetId() const = 0;

    /** SemVer of this extension's implementation. Example "0.3.0". */
    virtual FString GetVersionName() const = 0;

    /** BPEEK_EXTENSION_API_VERSION the extension was compiled against.
     *  Core refuses to dispatch to extensions whose API version is
     *  higher than core's (we haven't seen those methods yet). */
    virtual int32 GetAPIVersion() const { return BPEEK_EXTENSION_API_VERSION; }

    // --- Dispatch -------------------------------------------------------
    /** Cheap yes/no — return true only if this extension wants to render
     *  this asset. Scanner trusts the first extension that says yes,
     *  honouring priority order. Takes non-const UObject* to match the
     *  UE convention (most reflection calls aren't const-correct). */
    virtual bool CanHandle(UObject* Asset) const = 0;

    /** UClass list the scanner will TObjectIterator for this extension.
     *  Return concrete classes (not UObject::StaticClass()) so we don't
     *  walk the whole object graph. */
    virtual TArray<UClass*> GetHandledClasses() const = 0;

    /** Optional — leaf ClassName FNames for assets that aren't loaded
     *  in-memory (cooked-only flows). Most extensions return empty. */
    virtual TArray<FName> GetHandledClassNames() const { return {}; }

    // --- Render ---------------------------------------------------------
    /** Emit the asset's markdown into W. W is fresh per-asset — caller
     *  will SaveTo() after this returns. */
    virtual void Write(FBPeekMarkdownWriter& W,
                       UObject* Asset,
                       const FBPeekScanContext& Ctx) = 0;

    /** Optional — customise the _index.md row. Default implementation
     *  leaves Entry unchanged (scanner pre-fills AssetPath / DisplayName
     *  / RelativeMdPath from filesystem). */
    virtual void AppendIndexEntry(FBPeekIndexEntry& /*Entry*/,
                                  UObject* /*Asset*/) const {}

    /** Register this asset in the project-wide _index.md. Called by the
     *  scanner both on successful Write AND when Write is skipped (asset
     *  unchanged since last run — MD still valid). Core implementations
     *  forward to the matching FBPeekIndexBuilder::AddXxx call. Default
     *  no-op for extensions that don't participate in the index. */
    virtual void AppendToIndex(FBPeekIndexBuilder& /*IB*/,
                               UObject* /*Asset*/) const {}

    // --- Priority -------------------------------------------------------
    /** Higher wins when multiple extensions match a single asset. Core
     *  conventions:
     *    0   — generic fallback (DataAsset writer)
     *    100 — specific built-in writer (Blueprint, Enum, Struct, ...)
     *    200 — third-party extension targeting a specific plugin
     *           (BPeekFlow, BPeekGAS).
     *  Third parties who *replace* a built-in should use ≥ 150 and
     *  document why. */
    virtual int32 GetPriority() const { return 100; }
};
