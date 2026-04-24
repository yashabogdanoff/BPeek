# How BPeek works

One commandlet, one pass, two phases. Below is what happens between
`UnrealEditor-Cmd.exe -run=BPeekScan` and a populated
`Saved/BPeek/` directory.

## Launch

```bat
UnrealEditor-Cmd.exe "<Project>.uproject" -run=BPeekScan -unattended -nosplash -nop4
```

UE boots headless, UBT compiles the plugin if needed (~30–40 s on
first launch, ~3 s cached), the commandlet runs. Typical wall-time
on a ~1000-asset project: ~60–90 s for a full scan, ~2 s for
`-only-changed` incremental mode.

## Pipeline

```
Phase A — asset discovery + metadata collection
  ├── Asset Registry walk filtered by class name
  │     ("Blueprint", "WidgetBlueprint", "UserDefinedEnum",
  │      "DataTable", "World", "FlowAsset",
  │      "PrimaryDataAsset", "DataAsset", "LevelSequence",
  │      "BehaviorTree", "BlackboardData", …)
  ├── For each renderable asset: build a JSON record —
  │     asset_path, class, imports, components, widget tree,
  │     variables, data-table rows, level actors, sequence tracks
  └── Emit bpeek-metadata.json (consumable by external tools)

Phase B — markdown dispatch through IBPeekExtension registry
  ├── Union(Ext.GetHandledClasses for Ext in registry),
  │   drop descendant classes (avoid duplicate iteration)
  ├── For each root class:
  │     ForEachObjectOfClass(class, asset):
  │       1. Filter (skip /Script/ prefix, apply path whitelist/blacklist)
  │       2. FBPeekExtensionRegistry::FindFor(asset)
  │          → priority-sorted first winner
  │       3. Hash check — if ShouldRegenMd=false, skip the write but
  │          still call Ext.AppendToIndex so the _index.md row survives
  │       4. Otherwise Ext.Write(MdW, asset, ctx) → MdW.SaveTo(md_path)
  │                    Ext.AppendToIndex(IndexBuilder, asset)
  └── Summary: "MD dispatch: written=X skipped=Y across N extension(s)"

Phase C — finalisation
  ├── GameplayTags summary (DefaultGameplayTags.ini + discovered
  │   references from Phase A's JSON)
  ├── IndexBuilder → _index.md (stable-sorted by (Name, MdRelPath))
  ├── HashStore → _bpeek_hashes.json (for next -only-changed run)
  └── Coverage report → _bpeek_coverage.txt (Blueprint graph stats)
```

Phase A's JSON is a "who cites whom" reference graph. Phase B uses it
to populate the **Used by** sections in per-asset markdown.

## Asset discovery

Phase A uses hardcoded class-name lists in the scanner to decide which
assets participate:

```cpp
RenderableLeafNames = { "Blueprint", "WidgetBlueprint", "AnimBlueprint",
                        "UserDefinedEnum", "UserDefinedStruct", "DataTable",
                        "FlowAsset", "World", "PrimaryDataAsset",
                        "DataAsset", "LevelSequence",
                        "BehaviorTree", "BlackboardData", … }
```

These are strings only — the scanner does not link against Flow or
AIModule. It just knows the names so the Asset Registry can surface
matching assets. The actual rendering happens in the extensions that
own those types.

## Dispatch in detail

```cpp
// Source/BPeek/BPeekScanMetadataCommandlet.cpp (simplified)

const TArray<IBPeekExtension*> Exts = FBPeekExtensionRegistry::GetAll();

TSet<UClass*> RawClasses;
for (auto* E : Exts)
    for (UClass* C : E->GetHandledClasses()) RawClasses.Add(C);

// Drop descendants — iterating UBlueprint already hits Widget/Anim BPs.
TArray<UClass*> RootClasses = MinimalCover(RawClasses);

for (UClass* Cls : RootClasses)
{
    ForEachObjectOfClass(Cls, [&](UObject* Asset)
    {
        if (Asset->GetPathName().StartsWith("/Script/")) return;
        if (!BPeekMdWanted(Asset->GetPathName()))        return;

        IBPeekExtension* Ext = FBPeekExtensionRegistry::FindFor(Asset);
        if (!Ext) return;

        FString MdPath = OutDir / BPeekAssetPathToMdSubpath(Asset->GetPathName());

        if (!ShouldRegenMd(Asset, MdPath)) {
            Ext->AppendToIndex(IndexBuilder, Asset);   // keep the index row
            return;
        }

        FBPeekMarkdownWriter MdW;
        Ext->Write(MdW, Asset, Ctx);
        if (MdW.SaveTo(MdPath)) Ext->AppendToIndex(IndexBuilder, Asset);
    });
}
```

`FindFor` asks extensions in priority-descending order; the first one
whose `CanHandle` returns true wins. Core Blueprint extension runs at
priority 100, domain extensions (IMC, GAS, Flow, BT) at 200, the
generic `UDataAsset` fallback at 0.

## Incremental mode

`-only-changed` uses `_bpeek_hashes.json` emitted at the end of every
run — `{ asset_path → sha256(.uasset bytes) }`. Before calling a
renderer, the scanner compares the current hash against the stored
one. If unchanged and the previous markdown still exists, the write is
skipped but `AppendToIndex` still runs, so the `_index.md` row is
preserved.

Orphan cleanup also happens in this phase: markdown files in the
output that no longer correspond to a live asset (e.g. the source BP
was deleted) are removed.

Typical `-only-changed` run on Lyra (~1000 assets): ~2 s, 0–3 markdown
files rewritten, 0 orphans cleaned.

## Output layout

```
<Project>/Saved/BPeek/
├── _index.md               project-wide index (one row per asset, grouped by type)
├── _summary-by-type.md     same rows, flat by type header
├── _summary-by-module.md   rows regrouped by top two path segments
├── _bpeek_hashes.json      asset hashes for next -only-changed run
├── _bpeek_coverage.txt     BP graph-coverage stats
├── GameplayTags.md         tag listing + discovered usages
└── <mirror of the mount-point tree, one .md per asset>
```

See [04-output-formats.md](04-output-formats.md) for the per-file
schema.

## Why a commandlet and not an in-editor tool

The commandlet runs editor-only code without booting the editor UI —
we get full UE reflection (`TFieldIterator<FProperty>`,
`TObjectIterator<T>`, `UBlueprint::GeneratedClass`,
`FLinkerLoad::ImportMap`, `UEdGraph::Nodes`) and access to editor-only
data (`UserDefinedEnum`, `UserDefinedStruct`, `WidgetTree`, Blueprint
event graphs) which are stripped at cook time. Headless, no rendering,
finishes in ~60 s for a typical project.

The Tools → BPeek pulldown in the editor module is a thin wrapper —
same commandlet invoked from inside a running editor with different
scope flags (`-asset=` / `-bpeekmdfilter=`).
