# Development

Dev environment setup, build, tests, logging.

## Requirements

- **Unreal Engine 5.4+** installed locally (binary install from Epic
  Games Launcher is fine — BPeek compiles against engine headers +
  import libraries, no engine source needed).
- **Visual Studio 2022** with the C++ workload (Windows), or Rider
  with UE integration.
- **Git** for the codebase.

No .NET runtime, no external tools.

## Codebase layout

```
BPeek/
├── BPeek.uplugin
├── Config/
│   └── BaseBPeek.ini                  shipped settings documentation
├── Source/
│   ├── BPeekBuild/                    abstract ModuleRules base
│   ├── BPeekCompat/                   UE-version polyfill module
│   ├── BPeek/                         core runtime
│   │   ├── BPeek.Build.cs
│   │   ├── BPeekModule.{h,cpp}
│   │   ├── BPeekLog.{h,cpp}
│   │   ├── BPeekScanCommandlet.{h,cpp}
│   │   ├── BPeekScanMetadataCommandlet.{h,cpp}
│   │   ├── BPeekExtensionAPI.h        (IBPeekExtension + FBPeekScanContext)
│   │   ├── BPeekExtensionRegistry.{h,cpp}
│   │   ├── BPeekCoreExtensions.h      (built-in IBPeekExtension impls)
│   │   ├── BPeekSettings.{h,cpp}       (UDeveloperSettings)
│   │   ├── BPeek*Writer.h              (renderers)
│   │   ├── BPeekIndexBuilder.h
│   │   ├── BPeekGraphWalker.h          (BP `## Logic` section)
│   │   ├── BPeek*Helpers.h             (AssetPath, AssetLinks, TextUnwrap, …)
│   │   ├── BPeekHashStore.h            (incremental mode)
│   │   └── Tests/*.cpp                 (UE Automation tests)
│   ├── BPeekEditor/                   editor UI
│   ├── BPeekEnhancedInput/            IMC renderer
│   ├── BPeekGAS/                      GA/GE/AS renderer
│   ├── BPeekFlow/                     UFlowAsset renderer
│   └── BPeekBehaviorTree/             BT / BB renderer
├── Docs/
├── Scripts/
├── CHANGELOG.md
└── README.md
```

Each renderer submodule follows the same shape:
`<Module>/{<Module>.Build.cs, <Module>Module.{h,cpp}, <Module>Extension.h,
<Module>Writer.h}`.

## Build

UBT compiles the plugin when the host editor boots. First launch with
BPeek enabled triggers compilation automatically (~30–40 s on a cold
cache, ~3 s on warm cache).

Force a rebuild headlessly via the helper scripts:

```bat
Scripts\editor-deploy.bat  "<Host>"       :: copy source → patch uproject → compile
Scripts\deploy-and-run.bat "<Host>"       :: same + run scan + tear down
Scripts\run-tests.bat       "<Host>"      :: same + run automation tests
```

All three accept the host project path as the first positional arg, or
read `BPEEK_HOST` from env. UE root comes from `BPEEK_UE_ROOT`
(defaults to `G:\Epic Games\UE_5.4`).

If a compile breaks, look in `<Host>/Saved/Logs/<Project>.log` for the
UBT output. Common failure modes:

- `LNK2019` on a UE symbol → module missing from `PublicDependencyModuleNames`.
- `C1083` on a cross-module header → `PublicIncludePaths.Add(ModuleDirectory)`
  missing in the owning module's `Build.cs`.
- `LNK2019` on `GetPrivateStaticClass` → class used across modules
  without the `BPEEK_API` export macro.
- `FBPeekIndexBuilder::AddBlueprint` link errors → the submodule
  indirectly needs UMG symbols; add `"UMG"` + `"UMGEditor"` to the
  dependency list. See `BPeekGAS.Build.cs` for the canonical case.

## Pre-built release

```bat
Scripts\build-plugin.bat
```

Wraps `RunUAT BuildPlugin` to produce a release zip:

```
Releases/BPeek-v<VersionName>-UE<EngineVersion>-Win64/     (package dir)
Releases/BPeek-v<VersionName>-UE<EngineVersion>-Win64.zip  (shippable)
```

`EngineVersion` is auto-derived from `BPEEK_UE_ROOT` folder name
(`UE_5.4` → `5.4`). The release zip contains `BPeek.uplugin`, Win64
binaries, and `Source/` (kept so users with extra plugins like Flow
installed can recompile in-place). `Intermediate/` and `*.pdb` are
stripped. Around 780 KB for the default zip.

Two env vars control the distribution shape:

| Var                     | Effect                                                                                                                                                          |
|-------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `BPEEK_RELEASE_BUILD=1` | Forces `BPEEK_WITH_FLOW=0` so `BPeekFlow.dll` has no PE imports on `Flow.dll` — the resulting zip loads on hosts that haven't enabled Flow.                       |
| `BPEEK_PACKAGE_SUFFIX`  | Appended to the package + zip names. Used to publish a parallel Flow-enabled artifact (`-Flow`) next to the release-stub default.                                |

Built-in engine deps (EnhancedInput / GameplayAbilities / AIModule)
always ship as full integration: they're listed in `BPeek.uplugin`
without `Optional`, so UE auto-enables them on host mount. Flow is the
only community dep — can't be auto-enabled, hence the stub.

Users install by dropping the unzipped package into
`<Host>/Plugins/BPeek/`. `Scripts/deploy-prebuilt.bat <host>` automates
that for testing: copies the latest package into the host, patches the
`.uproject`, runs the scan, and leaves everything in place for repeat
runs.

## Tests

UE Automation tests live under each module's `Tests/` subfolder:

| Test group                | Coverage                                                |
|---------------------------|---------------------------------------------------------|
| `BPeek.AssetPath.*`       | Path normalisation, markdown subpath mapping            |
| `BPeek.AssetLinks.*`      | Linkify, path extraction, Unicode                       |
| `BPeek.TextUnwrap.*`      | Struct-text → human-readable                            |
| `BPeek.Extension.*`       | Registry behaviour, priority ordering, API-version gate |
| `BPeek.Semver.*`          | Semver parse / compare                                  |
| `BPeek.Version.*`         | `.uplugin` `BPeek` field compat check                   |
| `BPeek.InputMappings.*`   | IMC mapping blob parser                                 |

Run all:

```bat
Scripts\run-tests.bat "<Host>"
```

Takes a compilation + headless Automation pass, reports pass/fail at
the end.

## Adding a new asset type

Two shapes.

### A built-in renderer inside BPeek core

Use when the asset type is an engine-native class (always available in
any UE install, no plugin needed).

1. Add `BPeek<Type>Writer.h` alongside the existing writers in
   `Source/BPeek/`. Static
   `Write(FBPeekMarkdownWriter&, U<Type>*, Refs, Known, bVerboseMode)` shape.
2. In `BPeekCoreExtensions.h` add an adapter class implementing
   `IBPeekExtension`.
3. Register it in `FBPeekModule::StartupModule`.
4. If the type needs a dedicated bucket in `_index.md`, extend
   `FBPeekIndexBuilder` with an `AddXxxEntry(FEntry)` helper so core
   doesn't need to include the type's header directly.
5. Add an automation test under `Source/BPeek/Tests/`.

### A new domain submodule

Use when the asset type comes from an optional plugin (engine built-in
with `#if BPEEK_WITH_*` gating, or a community plugin needing
filesystem detection).

See [WRITING-AN-EXTENSION.md](WRITING-AN-EXTENSION.md). ~150 lines of
C++ + ~30 lines of `.Build.cs` + ~20 lines of module boilerplate gets
you a working renderer.

## Extending `FBPeekGraphWalker` (BP `## Logic`)

The walker in `Source/BPeek/BPeekGraphWalker.h` iterates
`UEdGraph::Nodes` via the UE editor API. Specialisations for
recognised `UK2Node_*` subclasses.

To handle a new K2 node type:

1. Find the class in UE (`Engine/Source/Editor/BlueprintGraph/Classes/K2Node_*.h`).
2. In `WriteNode` add a branch:
   `if (UK2Node_X* X = Cast<UK2Node_X>(N)) { … return; }`.
3. **Order matters** — specialised subclass branches go before their
   parents (same rule as C# pattern matching). Example: `UK2Node_DynamicCast`
   branch comes before `UK2Node_CallFunction` because Cast inherits from
   CallFunction.
4. Rebuild, run the scan, diff the output against the previous run.

## Logging

All traces go through `LogBPeek`. Verbosity conventions:

- **Display** — user-facing milestones (scan start/end, extension
  count, coverage summary).
- **Log** — module lifecycle, phase transitions.
- **Verbose** — per-asset dispatch decisions (`[dispatch] write bpeek.flow → /Game/…`),
  extension registration details.
- **VeryVerbose** — deep graph-walker traces.
- **Warning** — version-check mismatches, unusual but non-fatal data.
- **Error** — SaveTo failures, commandlet aborts.

Raise verbosity at runtime:

```bat
UnrealEditor-Cmd.exe … -LogCmds="LogBPeek Verbose"
```

Or permanently in `DefaultEngine.ini`:

```ini
[Core.Log]
LogBPeek=Verbose
```

## Troubleshooting

### "Cannot find plugin 'BPeek' referenced via .uproject"

`.uproject` lists BPeek but the plugin isn't on disk. Check
`<Host>/Plugins/BPeek/BPeek.uplugin` exists. If you're using the
deploy scripts, re-run `deploy-and-run.bat` or `deploy-prebuilt.bat`.

Minimum `.uproject` entry:

```json
"Plugins": [
    { "Name": "BPeek", "Enabled": true }
]
```

All submodules are declared inside `BPeek.uplugin`, so a single
`.uproject` entry enables the whole plugin.

### `LNK2019: unresolved external symbol U<X>::GetPrivateStaticClass`

A `UCLASS` is used across a module boundary without the export macro.
Add the right `_API`:

```cpp
UCLASS()
class BPEEK_API UBPeekScanCommandlet : public UCommandlet
```

### `C1083: Cannot open include file 'BPeek<Something>.h'`

Cross-module `#include` can't find the header. The owning module
needs:

```csharp
PublicIncludePaths.Add(ModuleDirectory);
```

Already present on BPeek, BPeekCompat, and the four renderer
submodules. Don't revert it.

### `[dispatch] write/skip` log lines missing

`LogBPeek` defaults to `Log` level; dispatch traces are `Verbose`.
Raise the verbosity to see them.

### `Logic` section shows `[??] UnknownNode`

`FBPeekGraphWalker::WriteNode` has no branch for that node class. Add
one as described in the Extending section above.

### MSVC 2022 complains about `TUniquePtr<IBPeekExtension>` destructor

Already handled — every renderer module's `.h` includes
`BPeekExtensionAPI.h` rather than forward-declaring `IBPeekExtension`.
If you're cloning the pattern for a new submodule, do the same.
