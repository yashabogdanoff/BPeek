# Architecture

BPeek is a UE 5.4+ editor plugin. Clone the repo into a project's
`Plugins/BPeek/` folder, open the editor (the plugin builds on first
launch), run the `BPeekScan` commandlet or hit the Tools → BPeek menu.
Output lands in `<Project>/Saved/BPeek/`.

## Repo layout

Flat, plugin-at-root:

```
BPeek/
├── BPeek.uplugin                       plugin descriptor
├── Config/
│   └── BaseBPeek.ini                   shipped settings documentation
├── Source/
│   ├── BPeekBuild/                     abstract ModuleRules base
│   ├── BPeekCompat/                    UE-version polyfill module
│   ├── BPeek/                          core runtime — commandlets, registry, writers
│   ├── BPeekEditor/                    editor UI — Tools → BPeek pulldown
│   ├── BPeekEnhancedInput/             InputMappingContext renderer
│   ├── BPeekGAS/                       Gameplay Ability System renderer
│   ├── BPeekFlow/                      Flow plugin renderer
│   └── BPeekBehaviorTree/              BehaviorTree + Blackboard renderer
├── Docs/                               this directory
├── Scripts/                            dev + release batch files
├── CHANGELOG.md
├── LICENSE
└── README.md
```

One `.uplugin`, seven modules. End users install by dropping the
repo (or a pre-built release zip) into `Plugins/BPeek/` — no nested
extraction, no manual moves.

## Module roles

**`BPeekBuild`** — abstract `ModuleRules` base. Defines
`BPEEK_UE_5_4_OR_LATER` … `BPEEK_UE_5_8_OR_LATER` preprocessor macros
and surfaces them to every module that inherits from it. Not registered
in the `.uplugin`; UBT picks it up by scanning `.Build.cs` files.

**`BPeekCompat`** — real module for UE-version polyfills. Absorbs
API differences between engine versions so the rest of the codebase
stays macro-free.

**`BPeek`** (core runtime) — commandlets, extension registry, the
renderers for the engine-native asset types (Enum, Struct, DataTable,
Blueprint + Widget/Anim subclasses, Level, LevelSequence, DataAsset).
Zero hard dependencies on optional plugins.

**`BPeekEditor`** — the Tools → BPeek top-level pulldown menu with
scan / navigation / management actions, Content Browser context menus,
and the Project Settings page.

**`BPeekEnhancedInput` / `BPeekGAS` / `BPeekBehaviorTree`** — domain
renderers for engine built-in plugins (EnhancedInput,
GameplayAbilities, AIModule). These plugins ship with every vanilla
5.4 install, so the renderers are included in pre-built releases.

**`BPeekFlow`** — renderer for the community
[Flow](https://github.com/MothCocoon/FlowGraph) plugin by Moth Cocoon
(not part of UE itself). Pre-built releases omit this renderer
because Flow has a major-version break between 1.6 and 2.0 that can't
be covered by a single binary. Source-install users pick it up
automatically if Flow is installed in their project's or engine's
`Plugins/` folder.

All four submodules share the same pattern: gated by a `BPEEK_WITH_*`
preprocessor define populated at build time from filesystem detection
in the module's `.Build.cs`. When the target plugin isn't installed on
the build machine, the module compiles as an empty shell that
registers nothing at runtime and links against no optional symbols.

## The extension registry

Every renderer — built-in or optional — is an `IBPeekExtension`
registered with `IModularFeatures` in its module's `StartupModule`.
The commandlet pulls them via `FBPeekExtensionRegistry::GetAll`,
sorts by priority descending, and for each asset calls `FindFor`
which returns the highest-priority extension whose `CanHandle`
accepts the asset.

Priority bands:

| Priority | Tier                                        | Examples                                                        |
|----------|---------------------------------------------|-----------------------------------------------------------------|
| `0`      | Generic fallback                            | `FBPeekDataAssetExtension`                                      |
| `100`    | Specific built-in                           | Enum, Struct, DataTable, Blueprint, Level, LevelSequence        |
| `200`    | Specific optional domain                    | EnhancedInput (IMC), GAS (GA/GE/AS), Flow, BehaviorTree (BT/BB) |

Example: a gameplay ability Blueprint (`GA_Hero_Dash`) inherits
`UBlueprint`. Both the core Blueprint extension (priority 100) and
BPeekGAS (priority 200) accept it; GAS wins. If GAS happens not to be
in the build (plugin unavailable on the build machine), the core
Blueprint extension handles the asset and you still get a reasonable
markdown — just without the ability-specific table.

The `IBPeekExtension` interface:

```cpp
// Source/BPeek/BPeekExtensionAPI.h

class BPEEK_API IBPeekExtension : public IModularFeature
{
public:
    static FName GetModularFeatureName();   // always "BPeekExtension"

    virtual FName   GetId()          const = 0;
    virtual FString GetVersionName() const = 0;
    virtual int32   GetPriority()    const { return 100; }

    virtual TArray<UClass*> GetHandledClasses() const = 0;
    virtual bool            CanHandle(UObject* Asset) const = 0;

    virtual void Write(FBPeekMarkdownWriter& W, UObject* Asset,
                       const FBPeekScanContext& Ctx) = 0;

    virtual void AppendToIndex(FBPeekIndexBuilder& IB, UObject* Asset) const {}
};
```

See [WRITING-AN-EXTENSION.md](WRITING-AN-EXTENSION.md) for a hands-on
guide to adding a new submodule.

## Two-pass scan

The commandlet (`UBPeekScanCommandlet`) runs the work in two passes.

1. **Metadata collection.** Walks the Asset Registry and the in-memory
   `UObject` graph to build a project-wide reference map: asset paths,
   class names, imports, components, variables, widget trees, level
   actors, level sequence tracks, etc. Emits `bpeek-metadata.json`
   alongside the markdown for external consumers.

2. **Markdown dispatch.** For each renderable class, iterate live
   `UObject` instances, filter by path rules, ask the registry for the
   best extension, render, write to disk. One loop, zero hardcoded
   asset types in the commandlet.

The scanner has no knowledge of GAS, Flow, EnhancedInput, or BT at
compile time — those all live behind `BPEEK_WITH_*` defines in
their own submodules.

## Config layering

`UBPeekSettings` is a `UDeveloperSettings` subclass registered under
Project Settings → Plugins → BPeek. Defaults come from the C++
constructor (`Source/BPeek/BPeekSettings.cpp`). UE merges config from
several ini files at startup, each overriding the previous:

```
1. C++ CDO defaults                            Source/BPeek/BPeekSettings.cpp
2. Engine/Config/BaseBPeek.ini                 (rare)
3. <Plugin>/Config/BaseBPeek.ini               ships with the plugin as docs
4. <Project>/Config/DefaultBPeek.ini           project-level override, edited
                                               via Project Settings GUI or by
                                               hand, commits to your repo
5. <Project>/Saved/Config/.../BPeek.ini        per-user runtime overrides
```

Command-line flags (`-asset=`, `-bpeekmdfilter=`, `-verbose`, etc.)
override everything above.

`IncludePatterns` and `ExcludePatterns` are `TArray<FString>`. The
idiomatic ini form is UE's `+Key=Value` per-element syntax:

```ini
[/Script/BPeek.BPeekSettings]

+IncludePatterns=/Game/**
+IncludePatterns=/ShooterCore/**
+IncludePatterns=/ShooterMaps/**

+ExcludePatterns=/Game/Megascans/**
+ExcludePatterns=**/BakedStaticMeshActor*
```

Use `!IncludePatterns=ClearArray` first if you need to replace (not
extend) the CDO default list.

## Distribution

**Source install (contributor / Flow-user workflow).** Clone the repo
into `<Host>/Plugins/BPeek/`. First editor launch triggers UBT to
compile the plugin against the local engine. If Flow is installed in
the engine's `Plugins/Marketplace/Flow/` or in the host project's
`Plugins/Flow/`, `BPEEK_WITH_FLOW=1` gets picked up automatically and
`UFlowAsset` rendering becomes available.

**Pre-built release (vanilla-engine users).** Each GitHub release
ships a `.zip` containing only the plugin descriptor and Win64
binaries (`.dll` per module plus the `UnrealEditor.modules` manifest).
Users drop the zip into `Plugins/BPeek/`; no UBT compile, no source
code on the host. Release binaries are built against engine
built-ins only (EnhancedInput, GameplayAbilities, AIModule) — Flow
support is source-install only because Flow has major-version breaks
(1.6 vs 2.0) that can't be addressed in a single universal binary.

Release zip size: ~650 KB.

## Testing

The public test target is Lyra Starter Game — a free UE sample with
GameFeature plugins (`ShooterCore`, `ShooterExplorer`, `ShooterMaps`,
`TopDownArena`), GAS usage, IMC assets, and BT assets. It exercises
every renderer the plugin ships with. Any UE 5.4 project works as a
host — pass the path as the first CLI arg to the deploy scripts or
set `BPEEK_HOST` env var.

30 UE Automation tests cover the extension registry, semver / compat
gate, path and asset-link helpers, and the IMC mapping parser. Run
them headless with `Scripts\run-tests.bat "<Host>"`.
