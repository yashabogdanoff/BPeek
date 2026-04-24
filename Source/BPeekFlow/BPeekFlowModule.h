#pragma once
#include "Modules/ModuleManager.h"
#include "BPeekExtensionAPI.h"  // IBPeekExtension full definition — needed
                                // for TUniquePtr<IBPeekExtension> destructor
                                // on MSVC 2022 (UE 5.7+).

//
// Entry point for BPeekFlow. Registers FBPeekFlowExtension with
// IModularFeatures on startup; unregisters on shutdown.
//
// Only mounts when the Flow plugin is enabled in the project — see
// BPeekFlow.uplugin dependency list.
//
class FBPeekFlowModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TUniquePtr<IBPeekExtension> Extension;
};
