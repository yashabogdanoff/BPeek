#pragma once
#include "Modules/ModuleManager.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandList.h"
#include "AssetRegistry/AssetData.h"
#include "EditorStyleSet.h"

class FBPeekEditorCommands : public TCommands<FBPeekEditorCommands>
{
public:
    FBPeekEditorCommands()
        : TCommands<FBPeekEditorCommands>(
            TEXT("BPeekEditor"),
            NSLOCTEXT("Contexts", "BPeekEditor", "BPeek Editor"),
            NAME_None,
            FAppStyle::GetAppStyleSetName())
    {}

    virtual void RegisterCommands() override;

    TSharedPtr<FUICommandInfo> ScanProject;
};

class FBPeekEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    // Adds a top-level "BPeek" pulldown to the main menu bar (positioned
    // after Help). Replaces the older "Tools > BPeek" submenu — that
    // section grew to 10 entries and was crowding the Tools menu.
    void RegisterMainMenuEntry();
    // Fills the BPeek pulldown — called lazily by ToolMenus the first
    // time the user opens it.
    void PopulateBPeekMenu(class UToolMenu* Menu);
    void RegisterContentBrowserMenu();
    void RegisterContentBrowserFolderMenu();

    // Scan handlers — each invokes the commandlet with different flags/
    // scope. All report finish via ShowFinishedToast.
    void OnScanProjectClicked();
    void OnScanChangedOnlyClicked();
    void OnScanWithCompileAuditClicked();
    void OnScanActiveLevelClicked();
    void OnScanOpenedBlueprintClicked();
    void OnScanSelectedAssetsClicked(TArray<FAssetData> SelectedAssets);
    void OnScanSelectedFoldersClicked(TArray<FString> SelectedPaths);

    // Navigation — no commandlet run, just open stuff.
    void OnOpenOutputFolder();
    void OnOpenProjectIndex();
    void OnOpenOutputLog();

    // Management.
    void OnClearOutput();
    void OnOpenSettings();

    // Shared: run the commandlet with a pre-built -arg string and show
    // the finish toast. Returns the commandlet's exit code.
    int32 RunScanCommandlet(const FString& Params, const FString& SlowTaskText);
    void ShowFinishedToast(bool bSuccess, const FString& OutDir);

    TSharedPtr<FUICommandList> CommandList;
    FDelegateHandle ContentBrowserExtenderHandle;
    FDelegateHandle ContentBrowserPathExtenderHandle;
};
