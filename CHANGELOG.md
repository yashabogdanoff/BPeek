# Changelog

All notable changes to BPeek are documented here.

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [SemVer](https://semver.org/).

## [Unreleased]

### Added
- UE 5.5 support — pre-built release zips published per engine version
  (`BPeek-v1.0.0-UE5.4-Win64.zip`, `…-UE5.5-Win64.zip`).
- Flow-enabled parallel build artifacts (`…-UE5.X-Win64-Flow.zip`) for
  projects that already use the community Flow plugin.
- `BPEEK_PACKAGE_SUFFIX` env var (`Scripts/build-plugin.bat`) for
  parallel-suffixed release artifacts.

### Changed
- **Distribution model — Monolith-style hybrid.** Built-in engine
  plugin integrations (EnhancedInput, GameplayAbilities, AIModule) now
  ship as full DLLs in every release zip. `BPeek.uplugin` lists those
  three deps without `Optional`, so UE auto-enables them when BPeek
  mounts on a host that hadn't enabled them yet. Eliminates load
  failures on hosts like Cropout (no GAS) and Lyra (no Flow).
- Flow stays as a release-stub: `BPEEK_RELEASE_BUILD=1` forces
  `BPEEK_WITH_FLOW=0` so the public `BPeekFlow.dll` has no hard PE
  imports on `Flow.dll`. Loads cleanly on Flow-less hosts.
- Release zip retains `Source/` so users with Flow installed can
  recompile in-place and unlock full Flow rendering.

### Fixed
- Pre-built `BPeekGAS.dll` had hard PE imports on `GameplayAbilities.dll`,
  causing the entire BPeek plugin to fail loading on hosts that hadn't
  enabled the GameplayAbilities engine plugin (Cropout). Same class of
  failure for `BPeekEnhancedInput.dll` (Cropout) and `BPeekFlow.dll`
  (Lyra). All three resolved by the distribution-model change above.

## [1.0.0]

First release.

### Added
- Scanner commandlet (`-run=BPeekScan`) that walks a project's Asset
  Registry and UObject graph, dispatches each asset through an
  `IBPeekExtension` registry, and writes cross-linked markdown to
  `<Project>/Saved/BPeek/`.
- Engine-native renderers for Blueprint, Widget Blueprint, Anim
  Blueprint, UserDefinedEnum, UserDefinedStruct, DataTable, Level,
  LevelSequence, DataAsset (fallback), plus a project-wide gameplay
  tag summary.
- Optional renderers gated by build-time filesystem detection:
  - `BPeekEnhancedInput` — InputMappingContext (key / action /
    modifier tables).
  - `BPeekGAS` — GameplayAbility / GameplayEffect / AttributeSet and
    their Blueprint subclasses.
  - `BPeekBehaviorTree` — BehaviorTree + BlackboardData + BP
    subclasses of BTTaskNode / BTDecorator / BTService.
  - `BPeekFlow` — FlowAsset + FlowNodeBlueprint from the community
    [Flow plugin](https://github.com/MothCocoon/FlowGraph).
- Tools → BPeek editor pulldown with scan / navigation / management
  actions; Content Browser context menus on assets and folders.
- Project Settings → Plugins → BPeek page for include/exclude
  filter patterns, output directory, rendering toggles.
- Incremental mode (`-only-changed`) via asset-hash diff against a
  stored manifest.
- Pseudo-code (`## Logic`) emission for Blueprint graphs via a
  `UEdGraph`-based walker with support for K2 cast nodes, branches,
  switches, sequence nodes, macro instances, and comment-box
  attribution.
- AnimBP state-machine extraction (states + transitions).
- Coverage report (`_bpeek_coverage.txt`) summarising graph
  reachability per scan.
- Compact / verbose output layouts — compact mode splits Blueprint
  output into `<Name>.md` + `<Name>.logic.md` companion files with
  shortened paths, inline one-liner tables; verbose mode keeps
  everything in a single file with full markdown tables.
- Pre-built release zips per UE engine version produced by
  `Scripts/build-plugin.bat`.
- 30 UE Automation tests covering path helpers, asset-link resolution,
  the extension registry, semver comparison, the uplugin version
  compat gate, and the IMC mapping parser.

### Notes
- Requires a UE 5.4+ install (headless commandlet runs inside
  `UnrealEditor-Cmd.exe`).
- Windows 64-bit pre-built binaries only; build from source on
  other platforms.
