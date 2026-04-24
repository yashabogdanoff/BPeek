#include "BPeekModule.h"
#include "BPeekCoreExtensions.h"
#include "BPeekExtensionAPI.h"
#include "BPeekLog.h"
#include "BPeekVersion.h"
#include "BPeekVersionCheck.h"
#include "Features/IModularFeatures.h"

void FBPeekModule::StartupModule()
{
    UE_LOG(LogBPeek, Log,
        TEXT("[core] starting — BPeek %s, UE macros: 5.4=%d 5.5=%d 5.6=%d 5.7=%d 5.8=%d"),
        TEXT(BPEEK_PLUGIN_VERSION_NAME),
        BPEEK_UE_5_4_OR_LATER, BPEEK_UE_5_5_OR_LATER,
        BPEEK_UE_5_6_OR_LATER, BPEEK_UE_5_7_OR_LATER, BPEEK_UE_5_8_OR_LATER);

    // Core writers are registered as IBPeekExtension implementations so
    // the scanner dispatches every asset type — built-in or third-party
    // — through the same registry path.
    BuiltIns.Add(MakeUnique<FBPeekEnumExtension>());
    BuiltIns.Add(MakeUnique<FBPeekStructExtension>());
    BuiltIns.Add(MakeUnique<FBPeekDataTableExtension>());
    BuiltIns.Add(MakeUnique<FBPeekBlueprintExtension>());
    BuiltIns.Add(MakeUnique<FBPeekLevelExtension>());
    BuiltIns.Add(MakeUnique<FBPeekLevelSequenceExtension>());
    BuiltIns.Add(MakeUnique<FBPeekDataAssetExtension>());

    const FName FeatureName = IBPeekExtension::GetModularFeatureName();
    for (const TUniquePtr<IBPeekExtension>& Ext : BuiltIns)
    {
        IModularFeatures::Get().RegisterModularFeature(FeatureName, Ext.Get());
        UE_LOG(LogBPeek, Verbose, TEXT("[ext] registered built-in '%s' v%s"),
            *Ext->GetId().ToString(), *Ext->GetVersionName());
    }
    UE_LOG(LogBPeek, Log, TEXT("[ext] %d built-in extension(s) registered"), BuiltIns.Num());

    // Version gate — scan enabled plugins for "BPeek" uplugin field,
    // log compat for each. No effect yet since there are no third-party
    // BPeek extensions installed, but the machinery is live and ready
    // for BPeekGAS / BPeekEnhancedInput / BPeekFlow.
    FBPeekVersionCheck::RunStartupCheck();
}

void FBPeekModule::ShutdownModule()
{
    const int32 Count = BuiltIns.Num();
    const FName FeatureName = IBPeekExtension::GetModularFeatureName();
    for (const TUniquePtr<IBPeekExtension>& Ext : BuiltIns)
    {
        IModularFeatures::Get().UnregisterModularFeature(FeatureName, Ext.Get());
        UE_LOG(LogBPeek, Verbose, TEXT("[ext] unregistered built-in '%s'"),
            *Ext->GetId().ToString());
    }
    BuiltIns.Reset();
    UE_LOG(LogBPeek, Log, TEXT("[core] shutdown: %d built-in extension(s) released"), Count);
}

IMPLEMENT_MODULE(FBPeekModule, BPeek)
