#pragma once
#include "Modules/ModuleManager.h"
#include "BPeekExtensionAPI.h"  // IBPeekExtension full definition — needed
                                // for TUniquePtr<IBPeekExtension> destructor
                                // on MSVC 2022 (UE 5.7+).

//
// Entry point for BPeekEnhancedInput. Registers a single
// IBPeekExtension (FBPeekEnhancedInputExtension) with IModularFeatures
// on startup; unregisters on shutdown. Core BPeek's scan dispatch
// picks the registered extension up automatically.
//
class FBPeekEnhancedInputModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TUniquePtr<IBPeekExtension> Extension;
};
