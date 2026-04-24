#include "BPeekExtensionRegistry.h"
#include "BPeekLog.h"
#include "Algo/StableSort.h"
#include "Features/IModularFeatures.h"

TArray<IBPeekExtension*> FBPeekExtensionRegistry::GetAll()
{
    TArray<IBPeekExtension*> Out =
        IModularFeatures::Get().GetModularFeatureImplementations<IBPeekExtension>(
            IBPeekExtension::GetModularFeatureName());

    // Priority sort, high first. StableSort preserves relative order
    // among equal-priority extensions (IModularFeatures gave us
    // registration order).
    Algo::StableSort(Out, [](const IBPeekExtension* A, const IBPeekExtension* B)
    {
        return A->GetPriority() > B->GetPriority();
    });
    return Out;
}

IBPeekExtension* FBPeekExtensionRegistry::FindFor(UObject* Asset)
{
    if (!Asset) return nullptr;
    for (IBPeekExtension* Ext : GetAll())
    {
        if (!IsAPICompatible(Ext)) continue;
        if (Ext->CanHandle(Asset)) return Ext;
    }
    return nullptr;
}

bool FBPeekExtensionRegistry::IsClassHandled(const UClass* Cls)
{
    if (!Cls) return false;
    for (IBPeekExtension* Ext : GetAll())
    {
        if (!IsAPICompatible(Ext)) continue;
        for (UClass* Handled : Ext->GetHandledClasses())
        {
            if (!Handled) continue;
            if (Cls->IsChildOf(Handled)) return true;
        }
    }
    return false;
}

bool FBPeekExtensionRegistry::IsAPICompatible(const IBPeekExtension* Ext)
{
    if (!Ext) return false;
    const int32 ExtVersion = Ext->GetAPIVersion();
    if (ExtVersion > BPEEK_EXTENSION_API_VERSION)
    {
        // Extension compiled against a newer core than we are. It may
        // call into virtuals we haven't implemented yet — skip it.
        UE_LOG(LogBPeek, Warning,
            TEXT("[ext] '%s' v%s built against BPeek API %d > core API %d — disabled"),
            *Ext->GetId().ToString(), *Ext->GetVersionName(),
            ExtVersion, BPEEK_EXTENSION_API_VERSION);
        return false;
    }
    return true;
}
