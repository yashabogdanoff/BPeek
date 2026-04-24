#pragma once
#include "Modules/ModuleManager.h"
#include "BPeekExtensionAPI.h"  // IBPeekExtension full definition — needed
                                // for TUniquePtr<IBPeekExtension> destructor
                                // on MSVC 2022 (UE 5.7+).

//
// Entry point for BPeekGAS. Registers FBPeekGASExtension (abilities /
// effects / attribute sets) with IModularFeatures on startup.
//
// Only mounts when the GameplayAbilities plugin is enabled in the
// project — see BPeekGAS.uplugin dependency list.
//
class FBPeekGASModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TUniquePtr<IBPeekExtension> Extension;
};
