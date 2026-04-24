# Limitations

Honest list of what BPeek can't do and why, so you can decide before
installing whether the output fits your use case.

## Requires a UE install

BPeek is a commandlet — it needs `UnrealEditor-Cmd.exe` to boot the
reflection system, the Asset Registry, and the editor-only parts of
the Blueprint runtime. Without the editor, there's no way to walk
`UEdGraph::Nodes` or `TFieldIterator<FProperty>`. That rules out
plain-CI setups that don't have ~60 GB of editor on the runner.

If your pipeline does have the editor (a build-machine with UE
installed), BPeek runs headless — no GUI needed.

## Project has to compile

The scan runs inside a booted editor. If your project hits a compile
error on startup, the commandlet never reaches the scan phase and
you get no output.

For partially-broken projects: fix the compile first, then scan.
There's no "skip broken modules" fallback.

## Windows-only binaries

The pre-built release zips ship Win64 DLLs only. Other platforms are
not packaged right now — no Linux, no Mac. You can still build from
source on any platform UE supports; the code itself is platform-
agnostic, the release workflow just hasn't been extended yet.

## Logic section is a graph walker, not an interpreter

The `## Logic` markdown section comes from `FBPeekGraphWalker`, which
iterates `UEdGraph::Nodes` linearly through exec-pin chains. It's a
shallow, descriptive walk — not a runtime simulator. Specifically it
doesn't:

- **Resolve data-flow values.** Variable gets and literal pins show
  their names and types but the walker doesn't const-fold or inline
  computed values.
- **Detect back-edges cleanly.** When an exec chain loops back to a
  previously-visited node, the walker stops with a marker rather than
  following the cycle.
- **Unfold deep macro expansions.** `MacroInstance` nodes are walked
  with a depth cap (16 levels); anything deeper is truncated with
  `[recursion limit]`.
- **Annotate latent actions.** `Delay`, `MoveComponentTo`,
  `AsyncLoadAsset`, etc. render as regular function calls — the
  markdown doesn't flag them as async.

If you need a full bytecode interpreter or a runtime-accurate
simulator, BPeek isn't that tool.

## Optional renderers depend on build-time detection

Four renderers are gated by filesystem detection in their `.Build.cs`:

| Renderer                    | Detected via                                    |
|-----------------------------|-------------------------------------------------|
| `BPeekEnhancedInput` (IMC)  | `Engine/Plugins/EnhancedInput`                  |
| `BPeekGAS` (GA/GE/AS)       | `Engine/Plugins/Runtime/GameplayAbilities`      |
| `BPeekBehaviorTree`         | `Engine/Plugins/Runtime/AIModule`               |
| `BPeekFlow` (UFlowAsset)    | Engine/Marketplace, Engine/Plugins, or project  |

EnhancedInput / GameplayAbilities / AIModule are engine built-ins —
they ship with every vanilla 5.4 install, so pre-built release binaries
always include those three renderers.

[Flow](https://github.com/MothCocoon/FlowGraph) is a community plugin
by Moth Cocoon — not part of UE itself — with a **major version
break** between 1.6 and 2.0. A single binary can't support both.
Pre-built release zips ship **without** Flow support. If you use
Flow, build BPeek from source: clone the repo into your project's
`Plugins/BPeek/` and the `Build.cs` detects your installed Flow
version automatically, resulting in a compatible binary.

## Extensions don't cover every asset type yet

BPeek has specialised renderers for: Blueprint, Widget Blueprint, Anim
Blueprint, Enum, Struct, DataTable, Level, LevelSequence, DataAsset
(fallback), InputMappingContext, Gameplay Ability / Effect /
AttributeSet, Flow Asset, Behavior Tree, Blackboard Data.

Not yet covered: Materials, Niagara systems, Physics Assets, Animation
Sequences, Sound Cues, Metasound sources, State Trees, Smart Objects,
Mass Gameplay. These assets appear in `_index.md` as metadata-only
entries (class name and path) but don't get per-asset markdown.

Adding a new renderer is a self-contained submodule —
see [WRITING-AN-EXTENSION.md](WRITING-AN-EXTENSION.md).

## Not multi-threaded

The scanner is single-threaded. Asset Registry and UObject reflection
aren't safely concurrent in editor context. On a ~1000-asset project
the full scan takes ~60–90 s; on ~8000 assets (Lyra with all
GameFeature plugins whitelisted), ~3–4 min. `-only-changed`
incremental mode is fast enough that day-to-day iteration isn't
bottlenecked.

## Output is read-only

BPeek dumps data. It doesn't edit, refactor, or lint assets. The
`## Issues` section (with `-recompile`) surfaces existing UE-compiler
warnings — it doesn't add new lint rules of its own.

## No runtime hotloading

Changing the plugin source requires a UBT rebuild. The commandlet runs
fresh on each invocation — no persistent daemon, no IPC, no way to
push incremental updates into a running editor without restarting.
