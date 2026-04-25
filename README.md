# BPeek

**Blueprint and asset introspection for Unreal Engine 5.4+.** A single
editor plugin that scans your project and writes a tree of cross-linked
markdown — one file per Blueprint, widget, enum, struct, data table,
level, level sequence, input mapping, gameplay ability, flow asset,
behavior tree, and more. Feed it to an AI agent and ask questions
about the project without opening the editor or clicking through BP
graphs.

No external `.exe`. No .NET. Works on vanilla binary UE installs.

---

## Quickstart

### From a pre-built release (vanilla engine users)

1. Download the latest `BPeek-vX.Y.Z-UE5.4-Win64.zip` from the GitHub
   Releases page.
2. Extract it into `<YourProject>/Plugins/BPeek/` — you should end up
   with `<YourProject>/Plugins/BPeek/BPeek.uplugin` at that path.
3. Add BPeek to your `.uproject` plugins:

   ```json
   "Plugins": [
       { "Name": "BPeek", "Enabled": true }
   ]
   ```

4. Open the project in UE. No compile step — the release zip ships
   pre-built Win64 DLLs.
5. **Tools → BPeek → Scan project.** Markdown lands in
   `<YourProject>/Saved/BPeek/`.

### From source (Flow users or contributors)

The release zip ships `Source/` alongside the pre-built binaries — you
can recompile in-place against your engine + plugin set without
re-cloning. Or clone the repo directly.

1. Drop the unzipped package (or the cloned repo) into
   `<YourProject>/Plugins/BPeek/`.
2. Delete `Plugins/BPeek/Binaries/` so UBT rebuilds against your local
   engine.
3. Add the same `.uproject` entry as above.
4. Open the project — UBT compiles the plugin on first launch
   (~30–40 s cold, ~3 s warm).
5. Scan via **Tools → BPeek** menu or the headless commandlet:

   ```bat
   UnrealEditor-Cmd.exe YourProject.uproject -run=BPeekScan -unattended -nosplash -nop4
   ```

Source rebuild picks up the community **Flow** plugin automatically: if
`Engine/Plugins/Marketplace/Flow/` exists, `BPeekFlow.Build.cs` flips
`BPEEK_WITH_FLOW=1` and FlowAsset rendering switches on. The default
release zip ships a Flow stub (loads cleanly on hosts without Flow); a
parallel `-Flow` zip with the full integration is also published for
projects that already use Flow.

---

## What BPeek renders

BPeek covers the engine's native asset types plus a handful of popular
optional plugins. Each group activates separately — a domain submodule
is a no-op if its target plugin isn't available at build time, and
everything else keeps rendering.

**Engine types (always available).** These ship with every UE 5.4
install and don't need any extra plugins:

| Asset type                            | Rendered by                |
|---------------------------------------|----------------------------|
| Blueprint (inc. Widget / Anim BP)     | core                       |
| UserDefinedEnum                       | core                       |
| UserDefinedStruct                     | core                       |
| DataTable                             | core                       |
| Level (UWorld)                        | core                       |
| LevelSequence                         | core                       |
| DataAsset / PrimaryDataAsset          | core (generic fallback)    |
| Gameplay tag registry + usages        | core                       |

**Engine built-in plugins (always available on a vanilla 5.4 install).**
These plugins ship with the engine but have to be enabled per-project;
BPeek renders their assets if the host project uses them:

| Asset type                              | From plugin          | Rendered by              |
|-----------------------------------------|----------------------|--------------------------|
| InputMappingContext                     | EnhancedInput        | EnhancedInput submodule  |
| GameplayAbility / Effect / AttributeSet | GameplayAbilities    | GAS submodule            |
| BehaviorTree / BlackboardData           | AIModule             | BehaviorTree submodule   |

**Community / marketplace plugins (optional, build-time detected).**
These aren't part of UE itself — they're installed separately. Release
zips don't include their renderers because community plugins have
major-version breaks that can't be covered by a single binary; install
BPeek from source and the correct binary gets produced automatically.

| Asset type                       | From plugin                                                      | Rendered by       |
|----------------------------------|------------------------------------------------------------------|-------------------|
| FlowAsset + FlowNodeBlueprint    | [Flow](https://github.com/MothCocoon/FlowGraph) (Moth Cocoon)    | Flow submodule    |

More renderers are straightforward to add — see
[Docs/WRITING-AN-EXTENSION.md](Docs/WRITING-AN-EXTENSION.md).

---

## How it works, in one paragraph

The `BPeekScan` commandlet walks the Asset Registry and the in-memory
`UObject` graph, collects per-asset metadata, then dispatches every
asset through an `IBPeekExtension` registry. Core ships built-in
renderers for the engine-native asset types; submodules handle
domain-specific types (GAS / IMC / Flow / BT). Output mirrors the
project's mount-point layout — `/Game/UI/W_Menu.W_Menu` becomes
`Saved/BPeek/Game/UI/W_Menu.md`. Per-file layouts are tuned for AI
consumption (compact tables, inline one-liners, `.logic.md`
companions for Blueprint pseudo-code).

Long version: [Docs/02-how-it-works.md](Docs/02-how-it-works.md).

---

## Commandlet

```bat
UnrealEditor-Cmd.exe <Project>.uproject -run=BPeekScan [flags]
```

| Flag                              | Effect                                                       |
|-----------------------------------|--------------------------------------------------------------|
| `-bpeekmd=<path>`                 | Override output directory (default `<Project>/Saved/BPeek/`) |
| `-only-changed`                   | Incremental — hash-diff + regen only changed assets          |
| `-recompile`                      | Per-BP `CompileBlueprint` pass; surface compiler errors      |
| `-asset=<path1>,<path2>,…`        | Scope: specific asset paths only                             |
| `-bpeekmdfilter=<prefix>;<prefix>`| Scope: restrict to given path prefixes                       |
| `-verbose`                        | Expanded layout (full tables, single-file BP output)         |

The Tools → BPeek menu wraps the same commandlet with preset scopes:
whole project, changed-only, compile audit, active level + sublevels,
open Blueprints, folder or asset selection.

Full reference: [Docs/03-commands.md](Docs/03-commands.md).

---

## Configuration

Filter what gets scanned via Project Settings → Plugins → BPeek, or by
committing a `Config/DefaultBPeek.ini` in your project:

```ini
[/Script/BPeek.BPeekSettings]

+IncludePatterns=/Game/**
+IncludePatterns=/MyGameFeature/**

+ExcludePatterns=/Game/Megascans/**
+ExcludePatterns=**/BakedStaticMeshActor*
```

`+Key=Value` appends to the default list. `!IncludePatterns=ClearArray`
resets first if you need to replace the defaults entirely. Glob
syntax: `*` (not `/`), `**` (any segments), `?` (single char).

Shipped default patterns live in
[Config/BaseBPeek.ini](Config/BaseBPeek.ini) — commented-out reference
for every setting the plugin understands.

---

## Documentation

| File                                                          | Topic                                                |
|---------------------------------------------------------------|------------------------------------------------------|
| [Docs/README.md](Docs/README.md)                              | Docs index                                           |
| [Docs/01-architecture.md](Docs/01-architecture.md)            | Modules, extension registry, config layering        |
| [Docs/02-how-it-works.md](Docs/02-how-it-works.md)            | Scan pipeline from commandlet launch to markdown    |
| [Docs/03-commands.md](Docs/03-commands.md)                    | Commandlet flags, editor menu, helper scripts       |
| [Docs/04-output-formats.md](Docs/04-output-formats.md)        | What each markdown file looks like                  |
| [Docs/05-limitations.md](Docs/05-limitations.md)              | What the scanner can't see and why                  |
| [Docs/06-development.md](Docs/06-development.md)              | Dev setup, build, tests, logging, troubleshooting   |
| [Docs/WRITING-AN-EXTENSION.md](Docs/WRITING-AN-EXTENSION.md)  | Adding a new renderer as a submodule                |

---

## Engine support

- **UE 5.4, 5.5, 5.6, 5.7** — pre-built release zips published per
  engine version (`BPeek-vX.Y.Z-UE5.<minor>-Win64.zip`, plus
  `…-UE5.<minor>-Win64-Flow.zip` variants with the community Flow
  plugin linked in).
- Newer engine versions: build from source. `.Build.cs` files gate
  UE-version-specific polyfills via `BPEEK_UE_5_X_OR_LATER`
  preprocessor macros. No branches, no per-engine forks.
- Verified hosts per engine:
  - 5.4 / 5.5 — Lyra Starter Game, Cropout Sample Project, FlowSolo demo.
  - 5.6 / 5.7 — Lyra Starter Game, Cropout Sample Project, FlowGame demo
    (Flow 2.2).

---

## License

MIT — see [LICENSE](LICENSE).
