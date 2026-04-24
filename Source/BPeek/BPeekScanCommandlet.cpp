#include "BPeekScanCommandlet.h"
#include "BPeekScanMetadataCommandlet.h"
#include "BPeekIssuesWriter.h"
#include "BPeekLog.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

UBPeekScanCommandlet::UBPeekScanCommandlet()
{
    // Commandlet needs an editor, is noninteractive, logs to console.
    IsEditor = true;
    IsClient = false;
    IsServer = false;
    LogToConsole = true;
}

int32 UBPeekScanCommandlet::Main(const FString& Params)
{
    // Delegate to DumpMetadata, injecting a default -bpeekmd=<Project>/BPeek
    // if the caller didn't specify one. Everything else in Params
    // (additional -flags) passes through unchanged.
    FString ForwardedParams = Params;
    if (!ForwardedParams.Contains(TEXT("-bpeekmd="), ESearchCase::IgnoreCase))
    {
        // Saved/BPeek/ is the recommended default (TODO-output-location.md
        // variant C) — UE projects conventionally SCM-ignore Saved/, so MD
        // output stays out of commits by default without per-project
        // .gitignore/ignore.conf work.
        const FString DefaultOut = FPaths::ProjectSavedDir() / TEXT("BPeek");
        ForwardedParams += FString::Printf(TEXT(" -bpeekmd=\"%s\""), *DefaultOut);
        UE_LOG(LogBPeek, Display, TEXT("[BPeek] No -bpeekmd= supplied, defaulting to %s"), *DefaultOut);
    }

    // `-recompile` opt-in flag. Triggers per-BP FKismetEditorUtilities::
    // CompileBlueprint with a captured FCompilerResultsLog so the
    // ## Issues section picks up real compile messages. Expensive
    // (~10-30 minutes extra on a ~700-BP project), so off by default;
    // use only for audit passes.
    if (ForwardedParams.Contains(TEXT("-recompile"), ESearchCase::IgnoreCase))
    {
        FBPeekIssuesWriter::bEnableRecompile = true;
        UE_LOG(LogBPeek, Display,
            TEXT("[BPeek] -recompile enabled — per-BP compile messages will be captured (slower run)"));
    }

    UBPeekScanMetadataCommandlet* Inner = NewObject<UBPeekScanMetadataCommandlet>(
        GetTransientPackage(), UBPeekScanMetadataCommandlet::StaticClass());
    const int32 RC = Inner->Main(ForwardedParams);

    // Reset the static flag so a second in-process invocation without
    // -recompile wouldn't inherit the last run's state.
    FBPeekIssuesWriter::bEnableRecompile = false;
    return RC;
}