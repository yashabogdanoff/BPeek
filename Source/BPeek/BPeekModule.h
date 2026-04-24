#pragma once
#include "Modules/ModuleManager.h"

class IBPeekExtension;

class FBPeekModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    // Built-in extensions live inside the BPeek module. Heap-allocated
    // so we can Register/Unregister them by pointer through
    // IModularFeatures without worrying about lifetime coupling to the
    // module instance's stack layout.
    TArray<TUniquePtr<IBPeekExtension>> BuiltIns;
};
