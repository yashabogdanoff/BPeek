#include "BPeekEnhancedInputModule.h"
#include "BPeekLog.h"
#include "Features/IModularFeatures.h"

// Extension body only pulls in EnhancedInput headers when the plugin
// was detected at build time — otherwise this is an empty module that
// registers nothing and links against no optional symbols.
#if BPEEK_WITH_ENHANCEDINPUT
#include "BPeekEnhancedInputExtension.h"
#endif

void FBPeekEnhancedInputModule::StartupModule()
{
#if BPEEK_WITH_ENHANCEDINPUT
    Extension = MakeUnique<FBPeekEnhancedInputExtension>();
    IModularFeatures::Get().RegisterModularFeature(
        IBPeekExtension::GetModularFeatureName(), Extension.Get());
    UE_LOG(LogBPeek, Log,
        TEXT("[ext] %s v%s registered"),
        *Extension->GetId().ToString(), *Extension->GetVersionName());
#else
    UE_LOG(LogBPeek, Log,
        TEXT("[ext] BPeekEnhancedInput: EnhancedInput plugin unavailable — IMC writer disabled"));
#endif
}

void FBPeekEnhancedInputModule::ShutdownModule()
{
#if BPEEK_WITH_ENHANCEDINPUT
    if (Extension.IsValid())
    {
        const FName Id = Extension->GetId();
        IModularFeatures::Get().UnregisterModularFeature(
            IBPeekExtension::GetModularFeatureName(), Extension.Get());
        UE_LOG(LogBPeek, Log, TEXT("[ext] %s unregistered"), *Id.ToString());
    }
    Extension.Reset();
#endif
}

IMPLEMENT_MODULE(FBPeekEnhancedInputModule, BPeekEnhancedInput)
