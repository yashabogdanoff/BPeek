#pragma once
#include "CoreMinimal.h"
#include "BPeekMarkdownWriter.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraphNode.h"
#include "Editor.h"
#include "EditorValidatorSubsystem.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/UObjectToken.h"
#include "Misc/DataValidation.h"
#include "AssetRegistry/AssetData.h"

/**
 * Emits a `## Issues` section if the asset has known defects:
 *
 *   - UBlueprint::Status — BS_Dirty / BS_Error / BS_UpToDateWithWarnings
 *     surface as one row each. BS_UpToDate / BS_Unknown produce no row.
 *
 *   - UEditorValidatorSubsystem — runs every registered validator (Epic's
 *     built-ins plus any project/plugin `UEditorValidatorBase` subclass)
 *     and pipes the resulting FText issues into rows with Warning/Error
 *     severity.
 *
 * Section is **skipped entirely** when nothing is worth reporting, so a
 * clean MD file stays clean. Output format:
 *
 *   ## Issues (N)
 *
 *   - **Error** — Status: `BS_Error` (last compile failed)
 *   - **Warning** — Validator (MyNamingRule): BP class name does not start with "BP_"
 *
 * Full CompileBlueprint capture is opt-in via the `-recompile`
 * commandlet flag.
 */
class FBPeekIssuesWriter
{
public:
    struct FIssue
    {
        enum class ESeverity : uint8 { Info, Warning, Error };
        ESeverity Severity;
        FString   Category;   // "Status", "Validator", "Compile"
        FString   Message;
    };

    /** Tier 2 opt-in: when true, Write() also runs CompileBlueprint per
     *  UBlueprint and captures FCompilerResultsLog messages. Set by the
     *  commandlet from the -recompile flag before the dump loop starts.
     *  Default false (Tier 1 only — cheap Status + Validation path). */
    inline static bool bEnableRecompile = false;

    /** Public entry. Calls collectors, writes only if any issues found. */
    static void Write(FBPeekMarkdownWriter& W, UObject* Asset)
    {
        if (!Asset) return;

        TArray<FIssue> Issues;
        if (UBlueprint* BP = Cast<UBlueprint>(Asset))
        {
            CollectStatus(BP, Issues);
            if (bEnableRecompile) CollectCompile(BP, Issues);
        }
        CollectValidation(Asset, Issues);

        if (Issues.Num() == 0) return;
        EmitSection(W, Issues);
    }

private:
    /** Map EBlueprintStatus → a `## Issues` row (or none for clean states). */
    static void CollectStatus(UBlueprint* BP, TArray<FIssue>& Out)
    {
        if (!BP) return;
        const EBlueprintStatus S = BP->Status;
        if (S == BS_UpToDate || S == BS_Unknown || S == BS_BeingCreated)
            return;  // nothing to surface

        FIssue I;
        I.Category = TEXT("Status");
        switch (S)
        {
            case BS_Dirty:
                I.Severity = FIssue::ESeverity::Warning;
                I.Message = TEXT("`BS_Dirty` — asset modified since last compile");
                break;
            case BS_Error:
                I.Severity = FIssue::ESeverity::Error;
                I.Message = TEXT("`BS_Error` — last compile failed");
                break;
            case BS_UpToDateWithWarnings:
                I.Severity = FIssue::ESeverity::Warning;
                I.Message = TEXT("`BS_UpToDateWithWarnings` — compiled but with non-fatal warnings");
                break;
            default:
                // Unexpected — stay silent rather than emit a confusing row.
                return;
        }
        Out.Add(MoveTemp(I));
    }

    /** Tier 2: recompile the BP in-memory with SkipSaveUponCompilation
     *  so we capture the compiler's own Error/Warning stream without
     *  persisting anything to disk. Node attribution attempts to extract
     *  a UEdGraphNode* reference from FTokenizedMessage tokens and
     *  prepends its title to the message. */
    static void CollectCompile(UBlueprint* BP, TArray<FIssue>& Out)
    {
        if (!BP) return;

        FCompilerResultsLog Results;
        Results.bSilentMode = true;

        // Flags chosen to keep the host BP byte-identical on disk:
        // - SkipSave: no uasset write-back even if save-on-compile is on.
        // - SkipGarbageCollection: GC cycle isn't meaningful at batch scale.
        // - BatchCompile: hints the API we're doing many in a row.
        const EBlueprintCompileOptions Opts =
            EBlueprintCompileOptions::SkipGarbageCollection |
            EBlueprintCompileOptions::SkipSave |
            EBlueprintCompileOptions::BatchCompile;

        FKismetEditorUtilities::CompileBlueprint(BP, Opts, &Results);

        for (const TSharedRef<FTokenizedMessage>& Msg : Results.Messages)
        {
            const EMessageSeverity::Type Sev = Msg->GetSeverity();
            // Skip Info-level noise — there's a lot of it (optimisation
            // hints etc.), not actionable signal.
            if (Sev == EMessageSeverity::Info) continue;

            FIssue I;
            I.Category = TEXT("Compile");
            I.Message  = Msg->ToText().ToString();
            if (const FString NodeTitle = FindNodeTitleInMessage(Msg); !NodeTitle.IsEmpty())
                I.Message = FString::Printf(TEXT("at node `%s` — %s"), *NodeTitle, *I.Message);
            I.Severity =
                (Sev == EMessageSeverity::Error)             ? FIssue::ESeverity::Error :
                (Sev == EMessageSeverity::PerformanceWarning ||
                 Sev == EMessageSeverity::Warning)           ? FIssue::ESeverity::Warning :
                                                                FIssue::ESeverity::Info;
            Out.Add(MoveTemp(I));
        }
    }

    /** Walk a message's tokens looking for a UObject reference — if it
     *  points to a UEdGraphNode, we can annotate the issue with the node
     *  title (`at node "Set Actor Location"`). FTokenizedMessage is how
     *  the Kismet compiler tags messages with their source nodes. */
    static FString FindNodeTitleInMessage(const TSharedRef<FTokenizedMessage>& Msg)
    {
        for (const TSharedRef<IMessageToken>& Token : Msg->GetMessageTokens())
        {
            if (Token->GetType() != EMessageToken::Object) continue;
            const FUObjectToken* Obj = static_cast<const FUObjectToken*>(&Token.Get());
            UObject* Referenced = Obj->GetObject().Get();
            if (UEdGraphNode* Node = Cast<UEdGraphNode>(Referenced))
            {
                FString T = Node->GetNodeTitle(ENodeTitleType::MenuTitle).ToString();
                T.ReplaceInline(TEXT("\n"), TEXT(" "));
                T.ReplaceInline(TEXT("\r"), TEXT(" "));
                return T;
            }
        }
        return FString();
    }

    /** Run EditorValidatorSubsystem and convert FText issues → our struct. */
    static void CollectValidation(UObject* Asset, TArray<FIssue>& Out)
    {
        if (!Asset || !GEditor) return;
        UEditorValidatorSubsystem* Sub = GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>();
        if (!Sub) return;

        // Script usecase — programmatic invocation from a commandlet run.
        // bWasAssetLoadedForValidation=false because we loaded the asset
        // for BPeek dump, not for validation specifically.
        FDataValidationContext Ctx(
            /*InWasAssetLoadedForValidation=*/false,
            EDataValidationUsecase::Script,
            /*InAssociatedObjects=*/{});

        // IsObjectValidWithContext returns Valid/Invalid/NotValidated — we
        // read the context issues either way.
        Sub->IsObjectValidWithContext(Asset, Ctx);

        for (const FDataValidationContext::FIssue& Iss : Ctx.GetIssues())
        {
            FIssue I;
            I.Category = TEXT("Validator");
            I.Message  = Iss.Message.ToString();
            switch (Iss.Severity)
            {
                case EMessageSeverity::Error:
                    I.Severity = FIssue::ESeverity::Error;
                    break;
                case EMessageSeverity::Warning:
                case EMessageSeverity::PerformanceWarning:
                    I.Severity = FIssue::ESeverity::Warning;
                    break;
                default:
                    I.Severity = FIssue::ESeverity::Info;
                    break;
            }
            Out.Add(MoveTemp(I));
        }
    }

    /** Emit `## Issues (N)` header + sorted bullet list. */
    static void EmitSection(FBPeekMarkdownWriter& W, TArray<FIssue>& Issues)
    {
        // Error-first, then Warning, then Info — readers scan top-down.
        Issues.StableSort([](const FIssue& A, const FIssue& B){
            return (int32)A.Severity > (int32)B.Severity;
        });

        W.WriteHeading(2, FString::Printf(TEXT("Issues (%d)"), Issues.Num()));
        W.WriteLine();
        for (const FIssue& I : Issues)
        {
            const TCHAR* Label =
                (I.Severity == FIssue::ESeverity::Error)   ? TEXT("Error") :
                (I.Severity == FIssue::ESeverity::Warning) ? TEXT("Warning") :
                                                             TEXT("Info");
            if (I.Category.IsEmpty())
                W.WriteLine(FString::Printf(TEXT("- **%s** — %s"), Label, *I.Message));
            else
                W.WriteLine(FString::Printf(TEXT("- **%s** — %s: %s"),
                    Label, *I.Category, *I.Message));
        }
        W.WriteLine();
    }
};
