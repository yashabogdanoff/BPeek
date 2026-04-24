# Writing a BPeek extension

This doc walks through adding a new renderer to BPeek — a submodule
that teaches the scanner how to write markdown for a new asset type,
or to re-render an existing one with better domain knowledge.

The BehaviorTree renderer (`Source/BPeekBehaviorTree/`) is the most
recent worked example and the template we'll mirror below.

Skim [01-architecture.md](01-architecture.md) first — it explains the
registry, the scan-dispatch loop, and how `BPEEK_WITH_*` build-time
defines gate optional plugins.

## What an extension actually is

A renderer is a submodule inside `BPeek.uplugin`. It registers an
`IBPeekExtension` implementation with `IModularFeatures` at
`StartupModule`. The scanner picks it up automatically — no
hardcoding in core.

Each submodule is four small files:

```
Source/BPeekMyExt/
├── BPeekMyExt.Build.cs              (inherits BPeekBuild)
├── BPeekMyExtModule.h / .cpp        (registers + unregisters)
├── BPeekMyExtExtension.h            (the IBPeekExtension impl)
└── BPeekMyExtWriter.h               (pure rendering helpers, optional)
```

Total first-submodule footprint: ~150 lines of C++ plus a handful of
JSON lines in the parent `BPeek.uplugin`.

## Step 1 — scaffold the submodule

Create `Source/BPeekMyExt/` alongside the existing renderer submodules.

### `BPeekMyExt.Build.cs`

```csharp
using UnrealBuildTool;
using System.IO;

public class BPeekMyExt : BPeekBuild
{
    public BPeekMyExt(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "BPeek", "BPeekCompat"
        });

        // Detect the target plugin. Pattern mirrored from BPeekGAS /
        // BPeekFlow / BPeekBehaviorTree — filesystem check at build
        // time, emit BPEEK_WITH_* define, gracefully become an empty
        // module when the plugin is absent.
        bool bHasTarget = false;
        {
            string EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
            string[] SearchPaths = new[] {
                Path.Combine(EngineDir, "Plugins", "Runtime", "MyThing"),
                Path.Combine(EngineDir, "Plugins", "Marketplace", "MyThing"),
                Path.Combine(EngineDir, "Plugins", "MyThing"),
            };
            foreach (string P in SearchPaths)
            {
                if (Directory.Exists(P)) { bHasTarget = true; break; }
            }
        }

        if (bHasTarget)
        {
            PublicDependencyModuleNames.AddRange(new string[] {
                "MyThing",     // module names of the plugin you're decoding
                "UMG",         // only if your extension calls FBPeekIndexBuilder::AddBlueprint
                "UMGEditor",
            });
            PublicDefinitions.Add("BPEEK_WITH_MYTHING=1");
        }
        else
        {
            PublicDefinitions.Add("BPEEK_WITH_MYTHING=0");
        }
    }
}
```

Inherit from `BPeekBuild` (not `ModuleRules`) so your module receives
the `BPEEK_UE_5_X_OR_LATER` preprocessor macros automatically.

For **engine built-in plugins** (EnhancedInput, GameplayAbilities,
AIModule) the detection always succeeds on any vanilla 5.4 install —
your `.dll` will ship in release zips.

For **community plugins** (like Flow) the detection depends on what's
installed on the build machine. Release zips can't include code paths
that don't link on a clean engine; users who need your renderer
install from source and pick up `WITH_MYTHING=1` via their local
plugin install.

## Step 2 — register the submodule in the parent `.uplugin`

Edit the repo-root `BPeek.uplugin`:

```json
{
  "Modules": [
    …
    {
      "Name": "BPeekMyExt",
      "Type": "Editor",
      "LoadingPhase": "Default"
    }
  ],
  "Plugins": [
    …
    {
      "Name": "MyThing",
      "Enabled": true,
      "Optional": true
    }
  ]
}
```

Mark the target plugin as `Optional: true` so UE doesn't refuse to
mount BPeek when `MyThing` isn't installed.

## Step 3 — implement `IBPeekExtension`

`Source/BPeekMyExt/BPeekMyExtExtension.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "BPeekExtensionAPI.h"       // IBPeekExtension + FBPeekScanContext
#include "BPeekMarkdownWriter.h"
#include "BPeekIndexBuilder.h"
#include "BPeekUsedBy.h"
#include "BPeekAssetPathHelpers.h"

#include "MyThing/MyAsset.h"         // your target asset class

class FBPeekMyExtExtension : public IBPeekExtension
{
public:
    // --- Identity ---
    FName   GetId()          const override { return TEXT("bpeek.mythingext"); }
    FString GetVersionName() const override { return TEXT("0.1.0"); }
    int32   GetPriority()    const override { return 200; }   // beats core defaults

    // --- Dispatch: which classes am I interested in? ---
    TArray<UClass*> GetHandledClasses() const override
    {
        return { UMyAsset::StaticClass() };
    }

    // --- Dispatch: should I actually handle this specific asset? ---
    bool CanHandle(UObject* Asset) const override
    {
        UMyAsset* A = Cast<UMyAsset>(Asset);
        if (!A) return false;
        if (A->HasAnyFlags(RF_ClassDefaultObject)) return false;
        return true;
    }

    // --- Render ---
    void Write(FBPeekMarkdownWriter& W, UObject* Asset, const FBPeekScanContext& Ctx) override
    {
        UMyAsset* A = Cast<UMyAsset>(Asset);
        if (!A) return;

        const FString AssetPath = A->GetPathName();
        const FString DisplayPath = Ctx.bVerboseMode
            ? AssetPath
            : FBPeekAssetPath::Compact(AssetPath);

        W.WriteHeading(1, FString::Printf(TEXT("%s (MyThing)"), *A->GetName()));
        W.WriteLine();
        W.WriteMetaRowCode(TEXT("Asset path"), DisplayPath);
        W.WriteMetaRowCode(TEXT("Class"),      A->GetClass()->GetName());
        W.WriteLine();

        // Domain-specific rendering. Walk the asset fields, emit
        // tables, cross-reference other assets via FBPeekAssetLinks.
        // See FBPeekEnhancedInputExtension, FBPeekFlowExtension, or
        // FBPeekBehaviorTreeExtension for fully-worked examples.

        // Used-by block — last section on every asset's markdown.
        static const TMap<FString, TArray<FString>> EmptyRefs;
        static const TSet<FString> EmptyKnown;
        FBPeekUsedBy::Write(W,
            Ctx.Refs  ? *Ctx.Refs  : EmptyRefs,
            AssetPath,
            Ctx.Known ? *Ctx.Known : EmptyKnown);
    }

    // --- Index row ---
    void AppendToIndex(FBPeekIndexBuilder& IB, UObject* Asset) const override
    {
        // Option 1: route into an existing index bucket. For BP-derived
        // assets: IB.AddBlueprint(Cast<UBlueprint>(Asset)).
        // Option 2: typed-entry pattern (like AddFlowEntry) where your
        // extension builds the FEntry itself, so core doesn't need to
        // include the target plugin's headers.
    }
};
```

Key rules for `CanHandle`:

- Cheap yes/no only — it runs once per asset per scan.
- Don't deep-parse properties; save that for `Write`.
- Return false for CDOs. Most domain classes use `GetDefaultObject()`
  for template state; dumping the CDO twice is noise.

Key rules for `Write`:

- `MdW` is fresh per-asset — core calls `MdW.SaveTo(...)` after you
  return. Don't write to disk yourself.
- Treat `Ctx` as const. The only exception is the coverage-stats
  accumulator which certain core extensions update.
- Expect `Ctx.Refs` and `Ctx.Known` to be null in synthetic contexts
  (unit tests) — use the static empty fallbacks as shown.

## Step 4 — module boilerplate

`Source/BPeekMyExt/BPeekMyExtModule.h`:

```cpp
#pragma once
#include "Modules/ModuleManager.h"
#include "BPeekExtensionAPI.h"  // IBPeekExtension full definition — needed
                                // for TUniquePtr<IBPeekExtension> destructor
                                // on MSVC 2022.

class FBPeekMyExtModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TUniquePtr<IBPeekExtension> Extension;
};
```

`Source/BPeekMyExt/BPeekMyExtModule.cpp`:

```cpp
#include "BPeekMyExtModule.h"
#include "BPeekLog.h"
#include "Features/IModularFeatures.h"

#if BPEEK_WITH_MYTHING
#include "BPeekMyExtExtension.h"
#endif

void FBPeekMyExtModule::StartupModule()
{
#if BPEEK_WITH_MYTHING
    Extension = MakeUnique<FBPeekMyExtExtension>();
    IModularFeatures::Get().RegisterModularFeature(
        IBPeekExtension::GetModularFeatureName(), Extension.Get());
    UE_LOG(LogBPeek, Log, TEXT("[ext] %s v%s registered"),
        *Extension->GetId().ToString(), *Extension->GetVersionName());
#else
    UE_LOG(LogBPeek, Log,
        TEXT("[ext] BPeekMyExt: MyThing plugin unavailable — renderer disabled"));
#endif
}

void FBPeekMyExtModule::ShutdownModule()
{
#if BPEEK_WITH_MYTHING
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

IMPLEMENT_MODULE(FBPeekMyExtModule, BPeekMyExt)
```

The `#if BPEEK_WITH_MYTHING` guards let the module compile into an
empty shell when the target plugin isn't detected — no link errors,
no runtime registration.

## Step 5 — try it

Deploy the plugin into a host project and run a scan:

```bat
Scripts\deploy-and-run.bat "<Host>"
```

In the log look for:

```
LogPluginManager: Mounting Project plugin BPeek
LogBPeek: [ext] bpeek.mythingext v0.1.0 registered
LogBPeek: [BPeekScan] MD dispatch: written=X skipped=Y across N extension(s)
```

Enable `LogBPeek` at Verbose to see each dispatch decision:

```
LogBPeek: Verbose: [dispatch] write bpeek.mythingext → /Game/MyStuff/MA_Foo.MA_Foo
```

If you see `"MyThing plugin unavailable"` but the plugin is actually
installed, check the search paths in your `.Build.cs` — the
filesystem detection didn't match any of them.

## Priority — when multiple renderers could claim the same asset

`FindFor` asks renderers in priority-descending order. The first one
whose `CanHandle` returns true wins.

| Priority | Tier                                        | Examples                                                        |
|----------|---------------------------------------------|-----------------------------------------------------------------|
| `0`      | Generic fallback                            | `FBPeekDataAssetExtension`                                      |
| `100`    | Specific built-in                           | Blueprint, Enum, Struct, DataTable, Level, LevelSequence        |
| `150`    | Replacement for a built-in (document why!)  | *(none shipping)*                                               |
| `200`    | Specific optional domain                    | EnhancedInput, GAS, Flow, BehaviorTree                          |

Typical case: a gameplay ability BP (`GA_Hero_Dash`) inherits
`UBlueprint`. The core Blueprint extension (priority 100) would match
it; BPeekGAS (priority 200) also matches because its `CanHandle` walks
the ParentClass chain for GAS roots. GAS wins.

If you're deliberately overriding a built-in renderer for a specific
parent class, use priority 150 and write a comment explaining why.

## Testing

Tests live inside the submodule's own `Source/BPeekMyExt/Tests/`
folder. Use the standard UE `IMPLEMENT_SIMPLE_AUTOMATION_TEST` macro.
`Scripts/run-tests.bat` picks them up automatically.

Skeleton:

```cpp
// Source/BPeekMyExt/Tests/BPeekMyExtTests.cpp
#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../BPeekMyExtExtension.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FBPeekMyExtBasicTest,
    "BPeek.MyExt.Basic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBPeekMyExtBasicTest::RunTest(const FString&)
{
#if BPEEK_WITH_MYTHING
    FBPeekMyExtExtension Ext;
    TestTrue(TEXT("priority is 200"), Ext.GetPriority() == 200);
    // … more assertions …
#endif
    return true;
}
#endif
```

Test names starting with `BPeek.` are picked up by the filter the
test scripts use. Pattern: `BPeek.<ExtName>.<Case>`.

## Reference implementations

Browse these for idiomatic patterns:

- **`Source/BPeekEnhancedInput/`** — simplest real renderer. Inherits
  the generic DataAsset rendering and adds one domain-specific table
  (IMC key/action/modifier mappings). Good template for extending an
  existing asset category.
- **`Source/BPeekGAS/`** — parent-class detection across a Blueprint
  hierarchy. Good template when your target plugin is used primarily
  through BP subclasses.
- **`Source/BPeekFlow/`** — wraps an existing writer class
  (`FBPeekFlowWriter`) and demonstrates the `FEntry`-passing pattern
  (`AddFlowEntry`) so core doesn't need to include your target
  plugin's headers.
- **`Source/BPeekBehaviorTree/`** — recursive tree walk with inline
  decorators/services. Good template when you need a hierarchical
  markdown layout.

## FAQ

**Do I need to modify BPeek core?** Usually no. The extension API is
enough for most renderers. If you hit a wall (a helper that should be
public, a new index bucket type), open an issue with the concrete use
case.

**What if my target plugin is not enabled in the user's project?**
The target plugin is marked `Optional: true` in `BPeek.uplugin`, so UE
mounts your module anyway. At runtime your extension registers, finds
no assets of its target type, and renders nothing. No errors.

**Can my submodule register multiple `IBPeekExtension` classes?** Yes
— instantiate and register each in `StartupModule`. Common reason: one
extension for the container asset, another higher-priority one for a
specific parent class.

**Where does my renderer's `.dll` go in release zips?** If your
filesystem detection succeeds on the build machine, yes — your module
produces `UnrealEditor-BPeekMyExt.dll` in the package `Binaries/Win64/`
folder. If detection fails (the target plugin isn't installed on the
build machine), you still get a `.dll`, just an empty one that
registers nothing.
