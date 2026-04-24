#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "BPeekSettings.generated.h"

/**
 * Project-level BPeek configuration, surfaced under
 *   Project Settings > Plugins > BPeek
 *
 * Stored in `Config/DefaultBPeek.ini` (standard UDeveloperSettings
 * layout). Every field can still be overridden at runtime by a
 * commandline flag — the settings are only the *defaults*.
 *
 * Precedence (commandlet):
 *   CLI flag > these settings > hard-coded fallbacks in the commandlet.
 */
UCLASS(config=BPeek, defaultconfig,
       meta=(DisplayName="BPeek"))
class BPEEK_API UBPeekSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UBPeekSettings();

    // Surface under "Project Settings > Plugins > BPeek" (not Editor
    // / Engine sections). Default is Plugins.
    virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }

    /** Where MD output is written. Empty = use engine default
     *  (<Project>/Saved/BPeek/). Supports ${ProjectDir}, ${SavedDir}
     *  tokens — see ResolveOutputDirectory below. */
    UPROPERTY(EditAnywhere, config, Category = "Output",
        meta = (DisplayName = "Output directory",
                Tooltip = "Where BPeek writes its markdown dumps. Leave blank to use Saved/BPeek. ProjectDir and SavedDir tokens supported."))
    FDirectoryPath OutputDirectory;

    /** Glob patterns for assets to include. Array-of-strings so ini files
     *  can use the idiomatic `+IncludePatterns=/Game/**` multi-line syntax.
     *  Empty array = include everything the scanner touches. */
    UPROPERTY(EditAnywhere, config, Category = "Filtering",
        meta = (DisplayName = "Include patterns",
                Tooltip = "Glob patterns for assets to include. Use ** for multi-segment wildcard. Empty = everything."))
    TArray<FString> IncludePatterns;

    /** Glob patterns to exclude from the dump. Applied after Include. */
    UPROPERTY(EditAnywhere, config, Category = "Filtering",
        meta = (DisplayName = "Exclude patterns",
                Tooltip = "Glob patterns to exclude. Applied after Include patterns."))
    TArray<FString> ExcludePatterns;

    /** Optional — path to a plaintext file with ADDITIONAL include
     *  patterns (one per line, # comments allowed, blank lines skipped).
     *  Contents are read at runtime and MERGED with IncludePatterns
     *  above — both contribute to the final filter. Useful for large
     *  pattern lists shared with teammates via a separate .txt. */
    UPROPERTY(EditAnywhere, config, Category = "Filtering",
        meta = (DisplayName = "External include file",
                Tooltip = "Optional .txt file — lines merged with Include Patterns at runtime. Useful for git-shared filter lists."))
    FFilePath ExternalIncludeFile;

    UPROPERTY(EditAnywhere, config, Category = "Filtering",
        meta = (DisplayName = "External exclude file",
                Tooltip = "Optional .txt file — lines merged with Exclude Patterns at runtime."))
    FFilePath ExternalExcludeFile;

    /** When true, per-BP MD writes include the long `## Logic` section
     *  (default). Off → skip walker entirely (compact dumps). */
    UPROPERTY(EditAnywhere, config, Category = "Rendering",
        meta = (DisplayName = "Emit ## Logic section"))
    bool bEmitLogicSection = true;

    /** When true, per-BP MD includes the `## Issues` section with
     *  Status + Validator signals. Off → skip (lean runs). */
    UPROPERTY(EditAnywhere, config, Category = "Rendering",
        meta = (DisplayName = "Emit ## Issues section"))
    bool bEmitIssuesSection = true;

    /** AI-optimised output is the default. Toggle this ON to get the
     *  expanded human-readable layout — full markdown tables, single-
     *  file Blueprint MD (no `.logic.md` companions), un-shortened asset
     *  paths. Useful for code-review, wiki-sync, or when reading the
     *  output directly in an editor. CLI flag `-verbose` overrides this. */
    UPROPERTY(EditAnywhere, config, Category = "Rendering",
        meta = (DisplayName = "Verbose mode (human-friendly)",
                Tooltip = "Toggle ON for expanded layout with markdown tables and single-file Blueprint MDs. Default is the compact AI-optimised layout."))
    bool bVerboseMode = false;

    /** Resolve output directory with ${ProjectDir}/${SavedDir}
     *  substitutions. Returns the engine default when the field is
     *  blank. */
    FString ResolveOutputDirectory() const
    {
        FString Dir = OutputDirectory.Path;
        if (Dir.IsEmpty())
            return FPaths::ProjectSavedDir() / TEXT("BPeek");
        Dir.ReplaceInline(TEXT("${ProjectDir}"), *FPaths::ProjectDir());
        Dir.ReplaceInline(TEXT("${SavedDir}"),   *FPaths::ProjectSavedDir());
        return Dir;
    }
};
