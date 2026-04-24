# Output formats

BPeek writes a directory of cross-linked markdown. Paths mirror the
project's UE mount-point layout — asset `/Game/UI/W_Menu.W_Menu`
becomes `<out>/Game/UI/W_Menu.md`.

## Top-level files

### `_index.md`

Flat project index, grouped by asset type. One row per scanned asset
with a name, a relative link to the per-asset markdown, and a short
one-liner (variable count, node count, class name, etc.).

Stable-sorted by `(Name, MdRelPath)` so runs against the same project
produce byte-identical output when nothing changed.

### `_summary-by-type.md`

Same rows as `_index.md` minus the summary header — a flat per-type
listing. For agents that want the bare list without the overview
wrapper.

### `_summary-by-module.md`

Rows regrouped by the first two path segments (e.g. `Game/UI`,
`Game/Characters`, `ShooterCore/Bot`). Useful when the layout
corresponds to logical modules and you want a "what's in this module"
view without grepping.

### `GameplayTags.md`

Every gameplay tag registered in the project:

- Tags from `Config/DefaultGameplayTags.ini`.
- Tags discovered while scanning (`FGameplayTag` properties, tag
  containers on GAS assets, IMC modifiers, Flow-node tags, BT
  blackboard keys referenced as tags).

Each tag row lists the assets that reference it — a quick way to see
where a tag gets attached from.

### `_bpeek_hashes.json`

Map of `asset_path → sha256(.uasset bytes)`. Written at the end of
every run, consumed by `-only-changed` on the next run. Safe to
delete to force a full rebuild.

### `_bpeek_coverage.txt`

Blueprint graph-coverage stats: reachable vs orphan exec nodes,
comment boxes, local variables, AnimBP state machines / states /
transitions, coverage percentages. Informational — no action needed
unless coverage numbers drop after a refactor.

## Per-asset markdown

Each asset gets one `.md` file whose shape matches its class. Two
layouts:

- **Default (compact, AI-optimised).** Blueprint output is split into
  a small `<Name>.md` header and a companion `<Name>.logic.md` with
  the pseudo-code walker output. Other asset types fit in a single
  file. Paths shortened to the last two segments where possible;
  tables collapse into inline one-liners.
- **Verbose (`-verbose` flag or `bVerboseMode=True`).** Full markdown
  tables, single-file Blueprint output (no `.logic.md` companion),
  un-shortened paths. Useful when reading the output directly in an
  editor.

The examples below show the default compact layout. Each preview is
indented four spaces to keep the sample text out of the document's
own heading tree.

### Blueprint

Sample `<Out>/ShooterCore/Heroes/B_Hero_ShooterMannequin.md`:

    # B_Hero_ShooterMannequin (Blueprint)

    - Asset path: `ShooterCore/Heroes/B_Hero_ShooterMannequin`
    - Parent: LyraCharacter

    **Components (5):** `CapsuleComponent`, `Mesh:SkeletalMeshComponent`,
    `CameraBoom:SpringArmComponent`, `FollowCamera:CameraComponent`,
    `HealthComponent:LyraHealthComponent`

    **Variables (8):** `DefaultWeapon`:`TSubclassOf<LyraWeaponInstance>`,
    `MaxHealth`:`float`=100, …

    ## Logic
    (see B_Hero_ShooterMannequin.logic.md)

    ## Used by
    - `ShooterCore/Experiences/HeroData_ShooterGame`

Companion `B_Hero_ShooterMannequin.logic.md` holds the pseudo-code
produced by `FBPeekGraphWalker`:

    # B_Hero_ShooterMannequin — logic

    ### function BeginPlay():
        Super::BeginPlay()
        HealthComponent.Initialize(MaxHealth)
        …

### Widget Blueprint / Anim Blueprint

Same shape as Blueprint, with extra sections:

- Widget BPs add a **Widget Tree** section (indented tree of widget
  names + classes).
- Anim BPs add a **State Machines** section with states and
  transitions.

### Enum

Sample `<Out>/ShooterCore/Game/E_MatchPhase.md`:

    # E_MatchPhase (Enum)

    - Asset path: `ShooterCore/Game/E_MatchPhase`

    | # | Name     | Display    | Tooltip                |
    |---|----------|------------|------------------------|
    | 0 | Warmup   | Warmup     | Pre-match countdown    |
    | 1 | Playing  | Playing    | Match in progress      |
    | 2 | PostGame | Post-game  | Match ended            |

### Struct (UserDefinedStruct)

Sample `<Out>/Game/Data/S_WeaponData.md`:

    # S_WeaponData (Struct)

    | Field       | Type             | Default | Tooltip                |
    |-------------|------------------|---------|------------------------|
    | WeaponClass | TSubclassOf<…>   |         | Weapon to spawn        |
    | BaseDamage  | float            | 20.0    | Damage per shot        |
    | AmmoCount   | int32            | 30      | Starting magazine size |

### DataTable

Sample `<Out>/Game/Data/DT_Weapons.md`:

    # DT_Weapons (Data Table)

    - Row struct: `S_WeaponData`
    - Rows: 8

    | Row      | WeaponClass        | BaseDamage | AmmoCount |
    |----------|--------------------|-----------:|----------:|
    | `Pistol` | `B_Weapon_Pistol`  |         15 |        12 |
    | `Rifle`  | `B_Weapon_Rifle`   |         25 |        30 |

### InputMappingContext (via `BPeekEnhancedInput`)

Sample `<Out>/Game/Input/IMC_Default.md`:

    # IMC_Default (Input Mapping Context)

    **Mappings (12):** `W`→`IA_Move`{SwizzleAxis,Negate},
    `A`→`IA_Move`{SwizzleAxis,Negate},
    `Space`→`IA_Jump`, …

Modifiers are bracketed after each key/action pair; chords render as
separate rows.

### GameplayAbility / Effect / AttributeSet (via `BPeekGAS`)

Sample `<Out>/ShooterCore/Heroes/Abilities/GA_Hero_Dash.md`:

    # GA_Hero_Dash (Gameplay Ability)

    - Asset path: `ShooterCore/Heroes/Abilities/GA_Hero_Dash`
    - Parent class: GA_AbilityWithWidget

    **Properties (6):** `AbilityTags`=`Ability.Type.Action.Dash`,
    `ActivationOwnedTags`=`Event.Movement.Dash`,
    `CooldownGameplayEffectClass`=`GE_HeroDash_Cooldown`,
    `NetExecutionPolicy`=`LocalPredicted`, …

### UFlowAsset (via `BPeekFlow`)

> Flow is a **community plugin** — [github.com/MothCocoon/FlowGraph](https://github.com/MothCocoon/FlowGraph)
> by Moth Cocoon. Not part of vanilla UE. Pre-built BPeek release zips
> don't include Flow rendering because the plugin has a major-version
> break between 1.6 and 2.0 that can't be covered by a single binary;
> build BPeek from source with Flow installed in your project or
> engine's Marketplace folder and the correct rendering is produced
> automatically.

Sample `<Out>/Game/Flows/F_Tutorial_Intro.md`:

    # F_Tutorial_Intro (Flow Asset)

    - Asset path: `Game/Flows/F_Tutorial_Intro`
    - Nodes: 14
    - Connections: 13

    ## Graph
    (mermaid diagram with n0 → n1 → … edges)

    ## Nodes
    ### Start
    - Class: `FlowNode_Start`
    - Id: <guid>
    …

### BehaviorTree / BlackboardData (via `BPeekBehaviorTree`)

Sample `<Out>/ShooterCore/Bot/BT/BT_Lyra_Shooter_Bot.md`:

    # BT_Lyra_Shooter_Bot (Behavior Tree)

    - Asset path: `ShooterCore/Bot/BT/BT_Lyra_Shooter_Bot`
    - Blackboard: `ShooterCore/Bot/BT/BB_Lyra_Shooter_Bot`
    - Nodes: 19

    ## Tree
    - **Selector**
      - _decorator:_ **Have Ammo [Blackboard]** — `BlackboardKey`=`OutOfAmmo`, …
      - **Selector**
        - _service:_ **RunEQS** — `EQSRequest`=`EQS_AIPerceptionEnemy`, …
        - _decorator:_ **Is Enemy Found [Blackboard]** — …
        - **Shoot And Move [Sequence]**
          - _service:_ **Shoot [BTS_Shoot_C]** — `TargetEnemy`=`TargetEnemy`
          - **Move To** — `AcceptableRadius`=25.0

Blackboard sample `<Out>/ShooterCore/Bot/BT/BB_Lyra_Shooter_Bot.md`:

    # BB_Lyra_Shooter_Bot (Blackboard Data)

    **Keys (4):** `TargetEnemy`:`Object`, `MoveGoal`:`Vector`,
    `OutOfAmmo`:`Bool`, `HasLineOfSight`:`Bool`

### Level

Sample `<Out>/ShooterMaps/L_Elimination.md`:

    # L_Elimination (Level)

    - Asset path: `ShooterMaps/L_Elimination`
    - Persistent actors: 47
    - Streaming levels: 3

    ## Actors
    - `BP_PlayerStart_Team1` (LyraTeamPlayerStart)
    - `BP_ObjectiveMarker` (BP_ObjectiveMarker_C)
    …

    ## Streaming
    - `L_Elimination_Geometry` (Always loaded)
    - `L_Elimination_Nav` (Always loaded)
    - `L_Elimination_Gameplay` (Blueprint)

### LevelSequence

Tracks grouped by binding (actor) with per-track section segments —
animation tracks, transform tracks, property tracks, event tracks.

### DataAsset (fallback)

Property table for any `UPrimaryDataAsset` / `UDataAsset` subclass
without a specialised renderer.

## `bpeek-metadata.json`

Side-car JSON emitted by `-run=BPeekScanMetadata`. Same data the
markdown is built from, suitable for external consumers: vector-store
embedders, project-graph visualisers, CI diff tools. Schema mirrors
the markdown sections (blueprints, enums, structs, data tables,
widget trees, level actors, sequence tracks).

Skip this step — just use `-run=BPeekScan` — if you only need the
markdown.
