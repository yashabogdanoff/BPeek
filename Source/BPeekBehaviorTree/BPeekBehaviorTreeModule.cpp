#include "BPeekBehaviorTreeModule.h"
#include "BPeekLog.h"
#include "Features/IModularFeatures.h"

// Extension body only pulls in BehaviorTree/BlackboardData headers when
// AIModule was detected at build time — otherwise this is an empty
// module that registers nothing and links against no optional symbols.
#if BPEEK_WITH_BEHAVIORTREE
#include "BPeekBehaviorTreeExtension.h"
#endif

void FBPeekBehaviorTreeModule::StartupModule()
{
#if BPEEK_WITH_BEHAVIORTREE
    Extension = MakeUnique<FBPeekBehaviorTreeExtension>();
    IModularFeatures::Get().RegisterModularFeature(
        IBPeekExtension::GetModularFeatureName(), Extension.Get());
    UE_LOG(LogBPeek, Log,
        TEXT("[ext] %s v%s registered"),
        *Extension->GetId().ToString(), *Extension->GetVersionName());
#else
    UE_LOG(LogBPeek, Log,
        TEXT("[ext] BPeekBehaviorTree: AIModule unavailable — BT/BB writer disabled"));
#endif
}

void FBPeekBehaviorTreeModule::ShutdownModule()
{
#if BPEEK_WITH_BEHAVIORTREE
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

IMPLEMENT_MODULE(FBPeekBehaviorTreeModule, BPeekBehaviorTree)
