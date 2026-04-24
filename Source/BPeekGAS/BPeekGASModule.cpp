#include "BPeekGASModule.h"
#include "BPeekLog.h"
#include "Features/IModularFeatures.h"

// Extension body only pulls in GameplayAbility/GameplayEffect headers
// when the GameplayAbilities plugin was detected at build time —
// otherwise this is an empty module that registers nothing and links
// against no optional symbols.
#if BPEEK_WITH_GAS
#include "BPeekGASExtension.h"
#endif

void FBPeekGASModule::StartupModule()
{
#if BPEEK_WITH_GAS
    Extension = MakeUnique<FBPeekGASExtension>();
    IModularFeatures::Get().RegisterModularFeature(
        IBPeekExtension::GetModularFeatureName(), Extension.Get());
    UE_LOG(LogBPeek, Log,
        TEXT("[ext] %s v%s registered"),
        *Extension->GetId().ToString(), *Extension->GetVersionName());
#else
    UE_LOG(LogBPeek, Log,
        TEXT("[ext] BPeekGAS: GameplayAbilities plugin unavailable — GAS writer disabled"));
#endif
}

void FBPeekGASModule::ShutdownModule()
{
#if BPEEK_WITH_GAS
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

IMPLEMENT_MODULE(FBPeekGASModule, BPeekGAS)
