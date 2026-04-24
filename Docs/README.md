# BPeek docs

Reference documentation for the BPeek plugin.

## What BPeek is

BPeek is a UE 5.4+ editor plugin that scans a project and writes a
markdown dump of every Blueprint, widget, enum, struct, data table,
data asset, level, level sequence, input mapping, gameplay ability,
gameplay effect, attribute set, flow asset, behavior tree, and
blackboard data it can find. The output is cross-linked and tuned for
AI-agent consumption — feed it to an LLM and ask questions about the
project without opening the editor.

## In scope

- You have the project source and a matching UE 5.4 install.
- The project compiles in the editor.
- You want a fast, AI-first mental model of an unfamiliar codebase.

## Not in scope

- CI runs without a UE install — BPeek needs the editor to start the
  commandlet.
- Projects that fail to compile — the commandlet can't boot.
- Reading the markdown manually — supported but a secondary use case.
  The layout and naming are tuned for embedding / RAG pipelines first.

## Reference docs

| File                                                  | Topic                                                |
|-------------------------------------------------------|------------------------------------------------------|
| [01-architecture.md](01-architecture.md)              | Modules, extension registry, config layering         |
| [02-how-it-works.md](02-how-it-works.md)              | Scan pipeline from commandlet launch to markdown     |
| [03-commands.md](03-commands.md)                      | Commandlet flags, editor menu, helper scripts        |
| [04-output-formats.md](04-output-formats.md)          | What each markdown file looks like                   |
| [05-limitations.md](05-limitations.md)                | What the scanner can't see and why                   |
| [06-development.md](06-development.md)                | Dev setup, build, tests, logging, troubleshooting    |
| [WRITING-AN-EXTENSION.md](WRITING-AN-EXTENSION.md)    | Adding a new asset renderer as an internal submodule |

## Quick start

```
1. Clone into <YourProject>/Plugins/BPeek/
2. Open the editor — the plugin builds on first launch
3. Tools → BPeek → Scan project
4. Read the output in <YourProject>/Saved/BPeek/
```

Or headless:

```bat
UnrealEditor-Cmd.exe YourProject.uproject -run=BPeekScan -unattended -nosplash -nop4
```

Full install and configuration: see the repo root [`../README.md`](../README.md).
