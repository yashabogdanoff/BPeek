#pragma once
#include "Modules/ModuleManager.h"
#include "BPeekExtensionAPI.h"  // IBPeekExtension full definition — needed
                                // for TUniquePtr<IBPeekExtension> destructor
                                // on MSVC 2022 (UE 5.7+). Forward declaration
                                // worked on 5.4 but trips C4150 on 5.7's
                                // stricter compiler.

//
// Entry point for BPeekBehaviorTree. Registers FBPeekBehaviorTreeExtension
// with IModularFeatures on startup (only when BPEEK_WITH_BEHAVIORTREE=1).
// Unregisters on shutdown.
//
// Disabled at build time if AIModule isn't detected in the engine — in
// that case this becomes a pure empty module and no extension is
// registered. Core BPeek's scan dispatch simply doesn't see a BT handler,
// and BT assets (if any exist in such a project) fall through to the
// generic dispatch path.
//
class FBPeekBehaviorTreeModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TUniquePtr<IBPeekExtension> Extension;
};
