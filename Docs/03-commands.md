# Commands

One headless entry point (the `BPeekScan` commandlet) plus a
Tools → BPeek pulldown that wraps it with preset scopes.

## Commandlet

```bat
UnrealEditor-Cmd.exe "<Project>.uproject" -run=BPeekScan -unattended -nosplash -nop4 [flags]
```

### Flags

| Flag                              | Effect                                                                                           |
|-----------------------------------|--------------------------------------------------------------------------------------------------|
| `-bpeekmd=<path>`                 | Override output directory. Default: `<Project>/Saved/BPeek/`.                                    |
| `-only-changed`                   | Incremental. Hash-diff each asset against `_bpeek_hashes.json`; regen only changed ones.         |
| `-recompile`                      | Per-BP `CompileBlueprint` pass. Adds compiler errors/warnings to each markdown's `## Issues`.    |
| `-asset=<path1>,<path2>,…`        | Scope: specific asset paths only. Paths use `/Game/…` form.                                      |
| `-bpeekmdfilter=<prefix>;<prefix>`| Scope: restrict to assets whose path starts with one of the given prefixes. Semicolon-separated. |
| `-verbose`                        | Expanded markdown layout — single-file BP output, full tables, un-shortened paths.               |
| `-unattended -nosplash -nop4`     | Standard UE flags for non-interactive runs.                                                      |

### Entry points

| Commandlet               | Purpose                                                                                          |
|--------------------------|--------------------------------------------------------------------------------------------------|
| `-run=BPeekScan`         | **Primary.** Thin alias over `BPeekScanMetadata` with sensible defaults.                         |
| `-run=BPeekScanMetadata` | The full pipeline. Emits `bpeek-metadata.json` side-car too. Use when you need the raw JSON.     |

Source: `Source/BPeek/BPeekScanCommandlet.cpp`,
`BPeekScanMetadataCommandlet.cpp`.

### Examples

Full project scan, default output:

```bat
UnrealEditor-Cmd.exe MyProject.uproject -run=BPeekScan -unattended -nosplash -nop4
```

Incremental rebuild:

```bat
UnrealEditor-Cmd.exe MyProject.uproject -run=BPeekScan -only-changed -unattended -nosplash -nop4
```

Only the `/ShooterCore/` subtree (useful against Lyra):

```bat
UnrealEditor-Cmd.exe LyraStarterGame.uproject -run=BPeekScan -bpeekmdfilter="/ShooterCore/" -unattended -nosplash -nop4
```

Pre-commit audit with compile pass:

```bat
UnrealEditor-Cmd.exe MyProject.uproject -run=BPeekScan -recompile -unattended -nosplash -nop4
```

Single asset (what "Scan opened blueprint" uses internally):

```bat
UnrealEditor-Cmd.exe MyProject.uproject -run=BPeekScan -asset="/Game/Foo/BP_Foo.BP_Foo" -unattended -nosplash -nop4
```

## Editor UI

The Tools menu bar gets a **BPeek** pulldown positioned right before Help.

### Scan

| Entry                             | Commandlet invocation                                                     |
|-----------------------------------|---------------------------------------------------------------------------|
| Scan project                      | `-run=BPeekScan` full project                                             |
| Scan changed only                 | `-run=BPeekScan -only-changed`                                            |
| Scan with compile audit           | `-run=BPeekScan -recompile`                                               |
| Scan active level (+ sublevels)   | `-run=BPeekScan -asset=<persistent>,<each streaming sublevel>`            |
| Scan opened blueprint             | `-run=BPeekScan -asset=<path of each BP open in an editor tab>`           |

Content Browser extras:

- **Asset context menu** — "Scan this asset with BPeek" (plural form
  for multi-selection).
- **Folder context menu** — "Scan this folder with BPeek (recursive)".

### Navigation

- Open output folder (`Saved/BPeek/` in Explorer).
- Open project index (`Saved/BPeek/_index.md` in the default markdown
  handler).
- Show output log, filtered to `LogBPeek`.

### Management

- Clear output — deletes the contents of `Saved/BPeek/` (with confirm
  dialog). Next scan is a full rebuild.
- Open settings — jumps to Project Settings → Plugins → BPeek.

## Helper scripts

Scripts in `Scripts/` cover the most common dev and release flows. They
take a host project as the first positional arg or read `BPEEK_HOST`
from env; UE root comes from `BPEEK_UE_ROOT`.

| Script                            | What it does                                                                                               |
|-----------------------------------|------------------------------------------------------------------------------------------------------------|
| `deploy-and-run.bat <host>`       | Copy source into host → patch `.uproject` → compile → run `BPeekScan` → clean up. Output stays in the host. |
| `deploy-prebuilt.bat <host>`      | Drop a pre-built release zip into the host → patch `.uproject` → run `BPeekScan`. No compile step.          |
| `editor-deploy.bat [--launch]`    | Deploy + compile for interactive use. Optionally launches the editor.                                       |
| `editor-cleanup.bat [--keep-md]`  | Remove a deployed plugin and restore `.uproject`. Optionally keeps the markdown output.                     |
| `run-tests.bat <host>`            | Deploy + compile + run automation tests (`BPeek.*` filter). Cleanup is automatic.                           |
| `run-bpeek.bat`                   | Standalone launcher intended to sit inside a host project — invokes the commandlet against the current project. |
| `build-plugin.bat`                | Wrap `RunUAT BuildPlugin` to produce a release zip (`Releases/BPeek-v<ver>-UE<UE>-Win64.zip`).              |
| `patch-uproject.ps1`              | Idempotent `.uproject` plugin-array patcher used by the deploy scripts.                                     |

`deploy-and-run.bat` is the developer workflow — tears everything down
at the end. `deploy-prebuilt.bat` is the end-user workflow — leaves the
plugin and patched `.uproject` in place, subsequent runs just
overwrite the DLLs.

All scripts default to UE 5.4 at `G:\Epic Games\UE_5.4\`. Override
with `BPEEK_UE_ROOT` environment variable.

## Output layout

```
<Project>/Saved/BPeek/
├── _index.md               project-wide asset index
├── _summary-by-type.md     per-type summary
├── _summary-by-module.md   per-module summary
├── _bpeek_hashes.json      run-to-run asset hash store (incremental mode)
├── _bpeek_coverage.txt     Blueprint coverage summary
├── GameplayTags.md         project-wide gameplay tag listing
└── <mirror of mount-point tree, one .md per asset>
```

Markdown files mirror asset paths — `/Game/UI/W_Menu.W_Menu` becomes
`<out>/Game/UI/W_Menu.md`. Per-file schema details in
[04-output-formats.md](04-output-formats.md).
