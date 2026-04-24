#include "BPeekEditorModule.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "LevelEditor.h"
#include "ContentBrowserModule.h"
#include "ContentBrowserDelegates.h"
#include "Misc/Paths.h"
#include "Misc/MessageDialog.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "ToolMenus.h"
#include "ISettingsModule.h"
#include "Framework/Docking/TabManager.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "Engine/Blueprint.h"
#include "Subsystems/AssetEditorSubsystem.h"
// Core module header — UI dispatches the real dump pipeline through the
// commandlet class. PrivateDependencyModuleNames in BPeekEditor.Build.cs
// pulls BPeek in.
#include "BPeekScanCommandlet.h"
#include "BPeekLog.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "FBPeekEditorModule"

void FBPeekEditorCommands::RegisterCommands()
{
    UI_COMMAND(ScanProject,
        "Scan project",
        "Scan the whole project and write markdown for every Blueprint / Flow / Level / DataTable / DataAsset",
        EUserInterfaceActionType::Button, FInputChord());
    // Per-scope variants (asset / folder / active level / opened BP) bind
    // lambdas directly — no FUICommandInfo needed.
}

void FBPeekEditorModule::StartupModule()
{
    FBPeekEditorCommands::Register();
    CommandList = MakeShared<FUICommandList>();

    CommandList->MapAction(
        FBPeekEditorCommands::Get().ScanProject,
        FExecuteAction::CreateRaw(this, &FBPeekEditorModule::OnScanProjectClicked),
        FCanExecuteAction());

    RegisterMainMenuEntry();
    RegisterContentBrowserMenu();
    RegisterContentBrowserFolderMenu();
}

void FBPeekEditorModule::ShutdownModule()
{
    if (FModuleManager::Get().IsModuleLoaded(TEXT("ContentBrowser")))
    {
        FContentBrowserModule& CB = FModuleManager::GetModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
        if (ContentBrowserExtenderHandle.IsValid())
        {
            CB.GetAllAssetViewContextMenuExtenders().RemoveAll(
                [this](const FContentBrowserMenuExtender_SelectedAssets& D) {
                    return D.GetHandle() == ContentBrowserExtenderHandle;
                });
        }
        if (ContentBrowserPathExtenderHandle.IsValid())
        {
            CB.GetAllPathViewContextMenuExtenders().RemoveAll(
                [this](const FContentBrowserMenuExtender_SelectedPaths& D) {
                    return D.GetHandle() == ContentBrowserPathExtenderHandle;
                });
        }
    }
    UToolMenus::UnregisterOwner(this);
    FBPeekEditorCommands::Unregister();
    CommandList.Reset();
}

// ---------------------------------------------------------------------
// Top-level "BPeek" pulldown (next to File / Edit / ... / Help).
// ---------------------------------------------------------------------

void FBPeekEditorModule::RegisterMainMenuEntry()
{
    // Deferred until ToolMenus is ready — StartupModule fires before the
    // main-frame menus are registered and direct ExtendMenu calls silently
    // no-op if done too early.
    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([this]()
    {
        FToolMenuOwnerScoped OwnerScoped(this);

        // Extend the menu bar itself (the strip with File/Edit/.../Help).
        UToolMenu* MenuBar = UToolMenus::Get()->ExtendMenu(TEXT("MainFrame.MainMenu"));
        if (!MenuBar) return;

        // Top-level bar entries (File / Edit / ... / Help) live in the
        // default section. AddSubMenu on a section gives us the variant
        // that takes a FNewToolMenuDelegate and returns FToolMenuEntry&
        // so we can pin the InsertPosition.
        FToolMenuSection& BarSection = MenuBar->FindOrAddSection(NAME_None);
        FToolMenuEntry& BPeekPulldown = BarSection.AddSubMenu(
            TEXT("BPeek"),
            LOCTEXT("BPeekMenuLabel", "BPeek"),
            LOCTEXT("BPeekMenuTooltip",
                "BPeek — scan project / asset / level and write markdown docs."),
            FNewToolMenuDelegate::CreateRaw(this, &FBPeekEditorModule::PopulateBPeekMenu));
        // Pin to the LEFT of "Help" — keeps Help as the last entry
        // (matches UE convention where Help stays right-most).
        BPeekPulldown.InsertPosition = FToolMenuInsert(TEXT("Help"), EToolMenuInsertType::Before);
    }));
}

void FBPeekEditorModule::PopulateBPeekMenu(UToolMenu* Menu)
{
    if (!Menu) return;

    const FSlateIcon ScanIcon(FAppStyle::GetAppStyleSetName(), TEXT("ContentBrowser.AssetActions"));
    const FSlateIcon NavIcon (FAppStyle::GetAppStyleSetName(), TEXT("Icons.Help"));

    // --- Scan actions ---------------------------------------------
    FToolMenuSection& ScanSection = Menu->FindOrAddSection(
        TEXT("BPeekScan"), LOCTEXT("BPeekScanSection", "Scan"));

    ScanSection.AddMenuEntry(
        TEXT("BPeekScanProject"),
        LOCTEXT("ScanProject", "Scan project"),
        LOCTEXT("ScanProjectTooltip",
            "Full project scan — write markdown for every Blueprint, Flow, Level, "
            "DataTable, DataAsset, etc. into Saved/BPeek/. ~1 min on medium projects."),
        ScanIcon,
        FUIAction(FExecuteAction::CreateRaw(this, &FBPeekEditorModule::OnScanProjectClicked)));

    ScanSection.AddMenuEntry(
        TEXT("BPeekScanChanged"),
        LOCTEXT("ScanChanged", "Scan changed only"),
        LOCTEXT("ScanChangedTooltip",
            "Hash-diff against the previous run's _bpeek_hashes.json — regenerate "
            "only assets whose .uasset changed, remove orphan MDs. Fast dev-loop."),
        ScanIcon,
        FUIAction(FExecuteAction::CreateRaw(this, &FBPeekEditorModule::OnScanChangedOnlyClicked)));

    ScanSection.AddMenuEntry(
        TEXT("BPeekScanAudit"),
        LOCTEXT("ScanAudit", "Scan with compile audit"),
        LOCTEXT("ScanAuditTooltip",
            "Full scan with -recompile: runs CompileBlueprint per BP and adds the "
            "compiler's Error / Warning messages to each ## Issues section. "
            "Significantly slower — meant for pre-commit / pre-release audits."),
        ScanIcon,
        FUIAction(FExecuteAction::CreateRaw(this, &FBPeekEditorModule::OnScanWithCompileAuditClicked)));

    ScanSection.AddMenuEntry(
        TEXT("BPeekScanLevel"),
        LOCTEXT("ScanLevel", "Scan active level (+ sublevels)"),
        LOCTEXT("ScanLevelTooltip",
            "Scan the currently open level and every sublevel it references "
            "(streaming sublevels + persistent level)."),
        ScanIcon,
        FUIAction(FExecuteAction::CreateRaw(this, &FBPeekEditorModule::OnScanActiveLevelClicked)));

    ScanSection.AddMenuEntry(
        TEXT("BPeekScanBP"),
        LOCTEXT("ScanBP", "Scan opened blueprint"),
        LOCTEXT("ScanBPTooltip",
            "Scan whichever Blueprint is currently open in a BP editor tab. "
            "If multiple BPs are open, all of them are scanned."),
        ScanIcon,
        FUIAction(FExecuteAction::CreateRaw(this, &FBPeekEditorModule::OnScanOpenedBlueprintClicked)));

    // --- Navigation -----------------------------------
    FToolMenuSection& NavSection = Menu->FindOrAddSection(
        TEXT("BPeekNav"), LOCTEXT("BPeekNavSection", "Navigation"));

    NavSection.AddMenuEntry(
        TEXT("BPeekOpenOutput"),
        LOCTEXT("OpenOutput", "Open output folder"),
        LOCTEXT("OpenOutputTooltip",
            "Open Saved/BPeek/ in your system file explorer."),
        NavIcon,
        FUIAction(FExecuteAction::CreateRaw(this, &FBPeekEditorModule::OnOpenOutputFolder)));

    NavSection.AddMenuEntry(
        TEXT("BPeekOpenIndex"),
        LOCTEXT("OpenIndex", "Open project index"),
        LOCTEXT("OpenIndexTooltip",
            "Open Saved/BPeek/_index.md — the auto-generated one-liner overview "
            "of every asset in the project."),
        NavIcon,
        FUIAction(FExecuteAction::CreateRaw(this, &FBPeekEditorModule::OnOpenProjectIndex)));

    NavSection.AddMenuEntry(
        TEXT("BPeekOpenLog"),
        LOCTEXT("OpenLog", "Show output log"),
        LOCTEXT("OpenLogTooltip",
            "Open the Window > Output Log tab — filter by LogBPeek to inspect "
            "the last scan's progress, warnings, and errors."),
        NavIcon,
        FUIAction(FExecuteAction::CreateRaw(this, &FBPeekEditorModule::OnOpenOutputLog)));

    // --- Management -----------------------------------
    FToolMenuSection& MgmtSection = Menu->FindOrAddSection(
        TEXT("BPeekMgmt"), LOCTEXT("BPeekMgmtSection", "Management"));

    MgmtSection.AddMenuEntry(
        TEXT("BPeekClear"),
        LOCTEXT("ClearOutput", "Clear output"),
        LOCTEXT("ClearOutputTooltip",
            "Delete every file inside Saved/BPeek/ (including _bpeek_hashes.json, "
            "so the next scan is a full rebuild). Confirms before deleting."),
        NavIcon,
        FUIAction(FExecuteAction::CreateRaw(this, &FBPeekEditorModule::OnClearOutput)));

    MgmtSection.AddMenuEntry(
        TEXT("BPeekSettings"),
        LOCTEXT("OpenSettings", "Open settings"),
        LOCTEXT("OpenSettingsTooltip",
            "Jump to Project Settings > Plugins > BPeek — Include/Exclude patterns, "
            "output directory, section toggles."),
        NavIcon,
        FUIAction(FExecuteAction::CreateRaw(this, &FBPeekEditorModule::OnOpenSettings)));
}

// ---------------------------------------------------------------------
// Content Browser context menus — asset + folder scope
// ---------------------------------------------------------------------

void FBPeekEditorModule::RegisterContentBrowserMenu()
{
    FContentBrowserModule& CB = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
    auto& Menus = CB.GetAllAssetViewContextMenuExtenders();

    FContentBrowserMenuExtender_SelectedAssets Extender =
        FContentBrowserMenuExtender_SelectedAssets::CreateLambda(
            [this](const TArray<FAssetData>& SelectedAssets) -> TSharedRef<FExtender>
    {
        TSharedRef<FExtender> Ext = MakeShared<FExtender>();
        Ext->AddMenuExtension(
            TEXT("AssetContextAdvancedActions"),
            EExtensionHook::After,
            CommandList,
            FMenuExtensionDelegate::CreateLambda([this, SelectedAssets](FMenuBuilder& Builder)
        {
            const FText Label = SelectedAssets.Num() == 1
                ? LOCTEXT("ScanSelectedOne", "Scan this asset with BPeek")
                : FText::Format(
                    LOCTEXT("ScanSelectedMany", "Scan {0} assets with BPeek"),
                    FText::AsNumber(SelectedAssets.Num()));
            Builder.AddMenuEntry(
                Label,
                LOCTEXT("ScanSelectedTooltip", "Write markdown for the selected asset(s) only"),
                FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ContentBrowser.AssetActions")),
                FUIAction(FExecuteAction::CreateLambda([this, SelectedAssets]()
                {
                    OnScanSelectedAssetsClicked(SelectedAssets);
                })));
        }));
        return Ext;
    });

    Menus.Add(Extender);
    ContentBrowserExtenderHandle = Menus.Last().GetHandle();
}

void FBPeekEditorModule::RegisterContentBrowserFolderMenu()
{
    FContentBrowserModule& CB = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
    auto& FolderMenus = CB.GetAllPathViewContextMenuExtenders();

    FContentBrowserMenuExtender_SelectedPaths FolderExtender =
        FContentBrowserMenuExtender_SelectedPaths::CreateLambda(
            [this](const TArray<FString>& SelectedPaths) -> TSharedRef<FExtender>
    {
        TSharedRef<FExtender> Ext = MakeShared<FExtender>();
        Ext->AddMenuExtension(
            TEXT("FolderContext"),
            EExtensionHook::After,
            CommandList,
            FMenuExtensionDelegate::CreateLambda([this, SelectedPaths](FMenuBuilder& Builder)
        {
            const FText Label = SelectedPaths.Num() == 1
                ? LOCTEXT("ScanFolderOne", "Scan this folder with BPeek (recursive)")
                : FText::Format(
                    LOCTEXT("ScanFolderMany", "Scan {0} folders with BPeek (recursive)"),
                    FText::AsNumber(SelectedPaths.Num()));
            Builder.AddMenuEntry(
                Label,
                LOCTEXT("ScanFolderTooltip", "Write markdown for every asset under the selected folder(s)"),
                FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ContentBrowser.AssetActions")),
                FUIAction(FExecuteAction::CreateLambda([this, SelectedPaths]()
                {
                    OnScanSelectedFoldersClicked(SelectedPaths);
                })));
        }));
        return Ext;
    });

    FolderMenus.Add(FolderExtender);
    ContentBrowserPathExtenderHandle = FolderMenus.Last().GetHandle();
}

// ---------------------------------------------------------------------
// Shared commandlet runner
// ---------------------------------------------------------------------

int32 FBPeekEditorModule::RunScanCommandlet(const FString& Params, const FString& /*SlowTaskText*/)
{
    const double StartTime = FPlatformTime::Seconds();
    UE_LOG(LogBPeek, Log, TEXT("[editor] invoking BPeek scan — %s"), *Params);

    UBPeekScanCommandlet* Cmd = NewObject<UBPeekScanCommandlet>(
        GetTransientPackage(), UBPeekScanCommandlet::StaticClass());
    const int32 Result = Cmd->Main(Params);

    const double Elapsed = FPlatformTime::Seconds() - StartTime;
    UE_LOG(LogBPeek, Log, TEXT("[editor] scan finished rc=%d in %.2fs"), Result, Elapsed);
    return Result;
}

// ---------------------------------------------------------------------
// Scan handlers
// ---------------------------------------------------------------------

void FBPeekEditorModule::OnScanProjectClicked()
{
    const FString OutDir = FPaths::ProjectSavedDir() / TEXT("BPeek");
    const FString Params = FString::Printf(TEXT("-bpeekmd=\"%s\""), *OutDir);
    const int32 RC = RunScanCommandlet(Params, TEXT("BPeek: scanning project…"));
    ShowFinishedToast(RC == 0, OutDir);
}

void FBPeekEditorModule::OnScanChangedOnlyClicked()
{
    const FString OutDir = FPaths::ProjectSavedDir() / TEXT("BPeek");
    const FString Params = FString::Printf(
        TEXT("-bpeekmd=\"%s\" -only-changed"), *OutDir);
    const int32 RC = RunScanCommandlet(Params, TEXT("BPeek: scanning changed…"));
    ShowFinishedToast(RC == 0, OutDir);
}

void FBPeekEditorModule::OnScanWithCompileAuditClicked()
{
    const FString OutDir = FPaths::ProjectSavedDir() / TEXT("BPeek");
    const FString Params = FString::Printf(
        TEXT("-bpeekmd=\"%s\" -recompile"), *OutDir);
    const int32 RC = RunScanCommandlet(Params, TEXT("BPeek: scanning with compile audit…"));
    ShowFinishedToast(RC == 0, OutDir);
}

void FBPeekEditorModule::OnScanActiveLevelClicked()
{
    if (!GEditor)
    {
        UE_LOG(LogBPeek, Warning, TEXT("[editor] GEditor unavailable — cannot scan active level"));
        return;
    }
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        UE_LOG(LogBPeek, Warning, TEXT("[editor] no active world — open a level first"));
        FNotificationInfo Info(LOCTEXT("NoActiveLevel", "BPeek: no active level."));
        Info.ExpireDuration = 4.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
        return;
    }

    TArray<FString> AssetPaths;

    // Persistent (main) world.
    if (UPackage* Pkg = World->GetOutermost())
    {
        const FString PkgName = Pkg->GetName();
        AssetPaths.Add(FString::Printf(TEXT("%s.%s"),
            *PkgName, *FPackageName::GetShortName(PkgName)));
    }

    // Streaming sublevels — covers "classic" composition maps. World
    // Partition loads actors rather than whole sublevels, so WP maps
    // show up only as their single persistent World; that's an accepted
    // limitation documented in the tooltip.
    for (ULevelStreaming* Streaming : World->GetStreamingLevels())
    {
        if (!Streaming) continue;
        const FSoftObjectPath WorldAsset = Streaming->GetWorldAsset().ToSoftObjectPath();
        if (WorldAsset.IsValid())
            AssetPaths.Add(WorldAsset.ToString());
    }

    if (AssetPaths.Num() == 0)
    {
        UE_LOG(LogBPeek, Warning, TEXT("[editor] active-level scan: no paths collected"));
        return;
    }

    const FString OutDir = FPaths::ProjectSavedDir() / TEXT("BPeek");
    const FString Params = FString::Printf(
        TEXT("-bpeekmd=\"%s\" -asset=\"%s\""),
        *OutDir, *FString::Join(AssetPaths, TEXT(",")));
    UE_LOG(LogBPeek, Log,
        TEXT("[editor] active-level scan: %d level(s) — %s"),
        AssetPaths.Num(), *FString::Join(AssetPaths, TEXT(", ")));
    const int32 RC = RunScanCommandlet(Params, TEXT("BPeek: scanning active level…"));
    ShowFinishedToast(RC == 0, OutDir);
}

void FBPeekEditorModule::OnScanOpenedBlueprintClicked()
{
    if (!GEditor)
    {
        UE_LOG(LogBPeek, Warning, TEXT("[editor] GEditor unavailable — cannot scan open BP"));
        return;
    }
    UAssetEditorSubsystem* AES = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (!AES)
    {
        UE_LOG(LogBPeek, Warning, TEXT("[editor] AssetEditorSubsystem missing"));
        return;
    }

    TArray<UObject*> OpenAssets = AES->GetAllEditedAssets();
    TArray<FString> BpPaths;
    for (UObject* Obj : OpenAssets)
    {
        if (UBlueprint* BP = Cast<UBlueprint>(Obj))
            BpPaths.Add(BP->GetPathName());
    }

    if (BpPaths.Num() == 0)
    {
        FNotificationInfo Info(LOCTEXT("NoBPOpen", "BPeek: no Blueprint open in an editor tab."));
        Info.ExpireDuration = 4.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
        return;
    }

    const FString OutDir = FPaths::ProjectSavedDir() / TEXT("BPeek");
    const FString Params = FString::Printf(
        TEXT("-bpeekmd=\"%s\" -asset=\"%s\""),
        *OutDir, *FString::Join(BpPaths, TEXT(",")));
    UE_LOG(LogBPeek, Log,
        TEXT("[editor] opened-BP scan: %d BP(s) — %s"),
        BpPaths.Num(), *FString::Join(BpPaths, TEXT(", ")));
    const int32 RC = RunScanCommandlet(Params, TEXT("BPeek: scanning opened BP(s)…"));
    ShowFinishedToast(RC == 0, OutDir);
}

void FBPeekEditorModule::OnScanSelectedAssetsClicked(TArray<FAssetData> SelectedAssets)
{
    if (SelectedAssets.Num() == 0) return;

    TArray<FString> Paths;
    Paths.Reserve(SelectedAssets.Num());
    for (const FAssetData& A : SelectedAssets)
        Paths.Add(A.GetObjectPathString());

    const FString OutDir = FPaths::ProjectSavedDir() / TEXT("BPeek");
    const FString Params = FString::Printf(
        TEXT("-bpeekmd=\"%s\" -asset=\"%s\""),
        *OutDir, *FString::Join(Paths, TEXT(",")));
    UE_LOG(LogBPeek, Log,
        TEXT("[editor] selected-assets scan: %d asset(s)"), SelectedAssets.Num());
    const int32 RC = RunScanCommandlet(Params, TEXT("BPeek: scanning selected asset(s)…"));
    ShowFinishedToast(RC == 0, OutDir);
}

void FBPeekEditorModule::OnScanSelectedFoldersClicked(TArray<FString> SelectedPaths)
{
    if (SelectedPaths.Num() == 0) return;

    TArray<FString> Prefixes;
    Prefixes.Reserve(SelectedPaths.Num());
    for (const FString& P : SelectedPaths)
    {
        FString Prefix = P;
        if (!Prefix.EndsWith(TEXT("/"))) Prefix += TEXT("/");
        Prefixes.Add(Prefix);
    }

    const FString OutDir = FPaths::ProjectSavedDir() / TEXT("BPeek");
    const FString Params = FString::Printf(
        TEXT("-bpeekmd=\"%s\" -bpeekmdfilter=\"%s\""),
        *OutDir, *FString::Join(Prefixes, TEXT(";")));
    UE_LOG(LogBPeek, Log,
        TEXT("[editor] selected-folders scan: %d folder(s)"), SelectedPaths.Num());
    const int32 RC = RunScanCommandlet(Params, TEXT("BPeek: scanning folder(s)…"));
    ShowFinishedToast(RC == 0, OutDir);
}

// ---------------------------------------------------------------------
// Navigation handlers
// ---------------------------------------------------------------------

void FBPeekEditorModule::OnOpenOutputFolder()
{
    const FString OutDir = FPaths::ProjectSavedDir() / TEXT("BPeek");
    if (!FPaths::DirectoryExists(OutDir))
    {
        IFileManager::Get().MakeDirectory(*OutDir, /*Tree*/ true);
    }
    FPlatformProcess::ExploreFolder(*OutDir);
    UE_LOG(LogBPeek, Log, TEXT("[editor] opened output folder %s"), *OutDir);
}

void FBPeekEditorModule::OnOpenProjectIndex()
{
    const FString IndexPath = FPaths::ProjectSavedDir() / TEXT("BPeek") / TEXT("_index.md");
    if (!FPaths::FileExists(IndexPath))
    {
        FNotificationInfo Info(LOCTEXT("IndexMissing",
            "BPeek: _index.md not found. Run a scan first."));
        Info.ExpireDuration = 4.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
        return;
    }
    FPlatformProcess::LaunchFileInDefaultExternalApplication(*IndexPath);
    UE_LOG(LogBPeek, Log, TEXT("[editor] opened project index %s"), *IndexPath);
}

void FBPeekEditorModule::OnOpenOutputLog()
{
    FGlobalTabmanager::Get()->TryInvokeTab(FName("OutputLog"));
    UE_LOG(LogBPeek, Log,
        TEXT("[editor] opened Output Log — filter by 'LogBPeek' to see BPeek entries"));
}

// ---------------------------------------------------------------------
// Management handlers
// ---------------------------------------------------------------------

void FBPeekEditorModule::OnClearOutput()
{
    const FString OutDir = FPaths::ProjectSavedDir() / TEXT("BPeek");

    const EAppReturnType::Type Choice = FMessageDialog::Open(
        EAppMsgType::OkCancel,
        FText::Format(
            LOCTEXT("ClearConfirm",
                "Delete every file inside:\n\n{0}\n\n"
                "_bpeek_hashes.json will also be removed, so the next scan is a full rebuild."),
            FText::FromString(OutDir)));
    if (Choice != EAppReturnType::Ok) return;

    if (!FPaths::DirectoryExists(OutDir))
    {
        UE_LOG(LogBPeek, Log, TEXT("[editor] clear-output: %s does not exist"), *OutDir);
        return;
    }

    IFileManager& FM = IFileManager::Get();
    const FString Glob = OutDir / TEXT("*");
    TArray<FString> Files, Dirs;
    FM.FindFiles(Files, *Glob, /*Files*/ true, /*Directories*/ false);
    FM.FindFiles(Dirs,  *Glob, /*Files*/ false, /*Directories*/ true);
    for (const FString& F : Files)
        FM.Delete(*(OutDir / F), /*RequireExists*/ false,
                  /*EvenReadOnly*/ true, /*Quiet*/ true);
    for (const FString& D : Dirs)
        FM.DeleteDirectory(*(OutDir / D), /*RequireExists*/ false, /*Tree*/ true);

    UE_LOG(LogBPeek, Log, TEXT("[editor] clear-output: removed %d file(s), %d subdir(s)"),
        Files.Num(), Dirs.Num());

    FNotificationInfo Info(FText::Format(
        LOCTEXT("ClearDone", "BPeek: cleared {0} file(s) and {1} subdir(s)."),
        FText::AsNumber(Files.Num()), FText::AsNumber(Dirs.Num())));
    Info.ExpireDuration = 4.0f;
    Info.bUseSuccessFailIcons = true;
    FSlateNotificationManager::Get().AddNotification(Info)
        ->SetCompletionState(SNotificationItem::CS_Success);
}

void FBPeekEditorModule::OnOpenSettings()
{
    ISettingsModule* SM = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
    if (SM)
    {
        // Section name = class name (UDeveloperSettings default is
        // GetClass()->GetFName() → "BPeekSettings").
        SM->ShowViewer(FName("Project"), FName("Plugins"), FName("BPeekSettings"));
        UE_LOG(LogBPeek, Log,
            TEXT("[editor] opened Project Settings > Plugins > BPeek"));
    }
    else
    {
        UE_LOG(LogBPeek, Warning,
            TEXT("[editor] Settings module unavailable — open Project Settings manually"));
    }
}

// ---------------------------------------------------------------------
// Shared finish toast
// ---------------------------------------------------------------------

void FBPeekEditorModule::ShowFinishedToast(bool bSuccess, const FString& OutDir)
{
    FNotificationInfo Info(bSuccess
        ? FText::Format(LOCTEXT("BPeekScanDone", "BPeek scan written to {0}"),
            FText::FromString(OutDir))
        : LOCTEXT("BPeekScanFailed", "BPeek scan failed — see Output Log"));
    Info.ExpireDuration = 6.0f;
    Info.bUseSuccessFailIcons = true;
    if (bSuccess)
    {
        Info.HyperlinkText = LOCTEXT("BPeekOpenFolder", "Open folder");
        Info.Hyperlink = FSimpleDelegate::CreateLambda([OutDir]()
        {
            FPlatformProcess::ExploreFolder(*OutDir);
        });
    }
    FSlateNotificationManager::Get().AddNotification(Info)
        ->SetCompletionState(bSuccess
            ? SNotificationItem::CS_Success
            : SNotificationItem::CS_Fail);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBPeekEditorModule, BPeekEditor)
