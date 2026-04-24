#include "BPeekFlowModule.h"
#include "BPeekLog.h"
#include "Features/IModularFeatures.h"

// Extension body only pulls in FlowAsset/FlowNode headers when the Flow
// plugin was detected in the host project at build time — otherwise this
// is an empty module that registers nothing and links against no
// optional symbols.
#if BPEEK_WITH_FLOW
#include "BPeekFlowExtension.h"
#endif

void FBPeekFlowModule::StartupModule()
{
#if BPEEK_WITH_FLOW
    Extension = MakeUnique<FBPeekFlowExtension>();
    IModularFeatures::Get().RegisterModularFeature(
        IBPeekExtension::GetModularFeatureName(), Extension.Get());
    UE_LOG(LogBPeek, Log,
        TEXT("[ext] %s v%s registered"),
        *Extension->GetId().ToString(), *Extension->GetVersionName());
#else
    UE_LOG(LogBPeek, Log,
        TEXT("[ext] BPeekFlow: Flow plugin unavailable in host project — Flow writer disabled"));
#endif
}

void FBPeekFlowModule::ShutdownModule()
{
#if BPEEK_WITH_FLOW
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

IMPLEMENT_MODULE(FBPeekFlowModule, BPeekFlow)
