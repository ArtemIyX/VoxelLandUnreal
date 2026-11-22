# Investigation: finite and infinite world assets, current-only formats

## Problem and expected behavior

The owner has replaced backward-compatibility requirements with a current-only plugin design. There must be two concrete world assets: finite worlds supporting whole-domain stages such as erosion, and infinite streamed worlds that reject operations requiring a complete finite world. This report is read-only source investigation; no build or tests were run.

## Observed behavior and root cause

The current `UVoxelLandM4WorldConfig` combines both modes behind editable `bUnlimited`, exposes finite domain and erosion fields beside local settings, and defaults its backend to `LegacyFloat`. Graph compilation has no required world-domain target, and node descriptors know only sample versus one-cell-filter execution. Consequently the asset class cannot currently invalidate nodes according to finite/infinite dependencies. Historical identity/program readers and multiple authoring output formats add compatibility work the owner no longer wants.

The existing graphify graph located relevant paths, but is stale (e.g. compiler entry line moved) and its broad query truncated; all material findings below were verified in source.

## Evidence ledger

| ID | Repository-relative location | Observed fact |
|---|---|---|
| E1 | `Source/VoxelLandData/Public/Data/Assets/VoxelLandM4WorldConfig.h:18,100,105,111` | One DataAsset type, `bUnlimited`, selectable old/current backend, finite cells/grid fields. |
| E2 | `Source/VoxelLandData/Private/Data/VoxelLandM4WorldConfig.cpp:571,597,609,668` | Capture validates both backend modes, rejects unlimited erosion, and writes mode from the boolean. |
| E3 | `Source/VoxelLand/Private/World/VoxelLandWorldSettingsInit.cpp:706,728,741,816,837` | Common preparation validates mode/backend, rejects unlimited global erosion, then branches local generation versus finite grid/prepass. |
| E4 | `Source/VoxelLandData/Public/Data/Structs/VoxelLandBuiltinNodeSpec.h:14`; `Data/Enums/VoxelLandOperatorStage.h:6` | Only Sample/OneCellFilter execution metadata; no world-dependency capability. |
| E5 | `Source/VoxelLandAlgo/Public/Operators/VoxelLandOperator.h:47` | External providers expose Stage, configuration and worker creation, but no finite/infinite dependency declaration. |
| E6 | `Source/VoxelLandAlgo/Public/Graph/VoxelLandGraphCompiler.h:15`; `Private/Graph/VoxelLandGraphCompiler.cpp:24,73` | Compile takes snapshot/providers but no world target; compiler emits current program v2 while readers still accept v1/v2. |
| E7 | `Source/VoxelLandEditor/Private/Graph/VoxelLandEditorSchema.cpp:316,326,349` | Menu filters by graph-function/control-graph type, then offers functions/providers; no world-domain filtering. |
| E8 | `Source/VoxelLandEditor/Private/VoxelLandAssetFactories.h:40,51`; `.cpp:61` | Existing world-settings and M4-config factories; no finite/infinite factories. |
| E9 | `Source/VoxelLandEditor/Private/Graph/VoxelLandGraphToolkit.cpp:1108,2710,2997` | Preview selects/casts M4 config and branches on bUnlimited. |
| E10 | `Source/VoxelLand/Private/World/VoxelLandM4WorldIdentity.cpp:43,240,250` | CaptureVersion and reader retain identities 1–6, selecting old versions based on enabled fields. |
| E11 | `Source/VoxelLandAlgo/Private/Graph/VoxelLandProgramCodec.cpp:351,357`; `Generation/VoxelLandProgram.cpp:368,440,568` | Old/new format branches and semantic behavior remain. |
| E12 | `Source/VoxelLandData/Public/Data/Assets/VoxelLandM4ControlGraph.h:21`; `Private/Data/VoxelLandM4ControlGraph.cpp:184` | Both current three-input height terminal and older separate outputs remain supported. |
| E13 | `Source/VoxelLandAlgo/Private/Generation/VoxelLandM5CandidatePlanner.cpp:11,125,269`; `Source/VoxelLand/Private/World/VoxelLandM5WorldIdentity.cpp:13,59` | M5 regional data still requires one finite global Bounds and all coordinates within ±1,048,576 cm. |
| E14 | `Source/VoxelLandAlgo/Private/Generation/VoxelLandM4FixedLattice.cpp:255,274,294,319` | Current large-coordinate fixed lattice allows Value2/Fbm2 and bounded scalar arithmetic, rejects other control opcodes. |
| E15 | `Source/VoxelLand/Private/World/VoxelLandWorld.cpp:247,326` | BeginPlay inert; explicit initialization and data-only dedicated-server branch must remain. |

## Chosen architecture

Use **two concrete world-definition assets**, `UVoxelLandFiniteWorld` and `UVoxelLandInfiniteWorld`, sharing an abstract `UVoxelLandWorldDefinition`. Keep one reusable terrain graph/function asset family. A graph is compiled/validated under the consuming world asset's immutable domain target; reusable functions propagate their transitive requirements. This preserves the owner's shared graph direction for game/mod content while making the world asset the authoritative capability boundary.

Do not implement the two classes as cosmetic wrappers around an editable mode boolean. Finite-only fields belong only to the finite class; infinite-only streaming/regional fields belong only to the infinite class. Common seed, voxel scale, graph references, shaping, materials and capsule settings belong to the base/common records. Snapshot stores a noneditable explicit world kind plus current semantics and captured capabilities.

## Capability contract

Add dedicated Data enum/record files for world kind and operation requirements, separate from execution stage. Suggested requirements: `LocalSample`, `BoundedNeighborhood` with explicit halo, `RegionalPlan` with bounded neighbor radius, and `FiniteWorld`.

- Both assets allow pure arithmetic/noise/shape operations and proven bounded filters.
- Infinite assets allow regional caves only with deterministic shared portals, bounded region dependencies, failure/closure policy and on-demand admission.
- Infinite assets reject global erosion, whole-world min/max/normalization/reductions, a global connected cave plan, finite-grid simulation and any provider with unknown/unbounded dependencies.
- Current erosion is configuration/stage-manifest driven, **not a graph node**. Validate it at the stage/config boundary too.
- Do not classify every Global-named stage as finite: a regional prepass is permissible if dependencies are explicitly bounded.
- SmoothDensity is a one-cell translation-invariant filter and may remain valid for infinite worlds when its halo is available. 3D noise is not intrinsically finite-only.
- Aggregate requirements through all function calls, every compiled channel and external provider descriptors. Unknown provider capability fails closed for infinite targets.
- Validate whole authored graphs, including disabled/unreachable forbidden nodes if the product rule says the asset invalidates them, and always validate the active dependency closure before execution. Retain editable invalid source with precise node diagnostics; clear invalid compiled output.

## Implementation plan and ownership

1. **Data/assets owner**: replace world settings/config hierarchy with the abstract base and two concrete assets in `VoxelLandData`; one enum/struct per dedicated file. Replace bUnlimited/backend dropdown with class-derived kind. Put erosion/grid domain only on finite class and regional streaming only on infinite. Capture one current immutable world snapshot. Keep common terrain graph/function assets. Update metadata, validators and cook guards. Existing user .uassets are work to preserve; provide an explicit opt-in development conversion/example regeneration if needed, do not delete them.
2. **Compiler/capabilities owner**: add domain context to compiler and serialized current program manifest; builtin/provider capability descriptors; transitive function requirement collection; matching runtime program validation and cook diagnostics; editor schema palette filtering is UX only, compiler/runtime are authoritative. Choose a single current program/identity encoding and reject noncurrent versions. Remove v1 branches, old separate M4 terminals/assignment UI and obsolete tests/readers. Remove LegacyFloat local backend and use/expand current fixed lattice for infinite controls; finite native-noise path remains an intentional current algorithm, not historical fallback.
3. **Runtime/editor owner**: update `AVoxelLandWorld` entry point and preparation request to accept abstract world definition and dispatch typed snapshots. Finite path runs one complete prepass/erosion; infinite path streams region/chunk inputs without any whole-world allocation. Update preview asset picker, bake/cook hooks, factories/actions/icons and lifecycle freshness. Keep BeginPlay inert and no render work on servers. Remove old saved-world restore APIs/identity versions; use one current descriptor whose world kind/capabilities/algorithm settings are hashed.
4. **Infinite coordinate follow-up (required, not rename)**: replace M5's ±10.48 km global plan envelope with global int64 region/chunk keys and checked bounded region-local coordinates; use canonical tuple seeds and fixed-point global sampling, floor division for negatives, region-relative rendering. Per-region plan bounds replace a global finite network domain. No promise of infinite address precision: documented representational limits still fail explicitly. Do not allocate/validate the entire coordinate universe.
5. **Tests/docs owner with implementation owners**: replace compatibility/golden-old-format tests with current-only format, obsolete version rejection and deterministic current-output references. Tests must cover asset type-specific fields, finite-only node nested in reusable function, provider unknown dependencies, all entry points/editor/cook/runtime, invalid source payload clearing, finite erosion acceptance and infinite erosion rejection, local neighborhood filter seam equality, negative region portals and per-world cancellation. Update canonical plan and M5 docs to remove superseded compatibility rules and record two-asset direction.

## Removal inventory

Remove historical readers/version switches (`CaptureVersion`, identity 1–5 retention, program v1, older prepass encodings), compatibility-only APIs and wrappers, LegacyFloat local generation, old three-terminal M4 output projection/assignment, tests whose sole aim is reproducing obsolete worlds, and docs promising old-world migration. Keep current format/version identifiers and unknown-version rejection: current-only does not mean unversioned data. Keep determinism tests and actual finite/local algorithm checks. Treat M4/M5 prefixes separately from the word legacy: rename milestone-facing public assets/API where useful, but do not confuse a mass rename with removing obsolete behavior.

## Risks and verification

- Shared graph used by both asset kinds must not silently change target; include world kind/capabilities in program key or validate a target-neutral requirement manifest before each use.
- Node palette filtering cannot protect cooked payloads, undo/reload, clipboard/paste, nested functions or external providers; runtime revalidation is required.
- Infinite planning currently remains finite-domain limited (E13); fix coordinate architecture before claiming Minecraft-like worlds.
- Deleting legacy reader branches must not delete user-authored untracked Content assets or unrelated edits. No broad git add/reset or folder deletion.
- Abstract base/factories require clean action registration/unregistration; runtime Data stays independent of Editor/Render.
- All existing asynchronous jobs need asset-kind/source-version freshness; old captured jobs must never publish after switching asset or graph target.
- No build/tests were run. Human must later build and execute current automation, editor asset creation/save/undo/cook, finite erosion inspection, infinite streaming/seams/portals and dedicated-server checks. Prior deferred M4 checks remain unverified.

## Applicable skills read

- `custom-asset-types`: runtime UObject containers; UFactory and asset actions in Editor; register/unregister actions cleanly.
- `uproperty-specifiers`: reflected owned configuration with categories/units; transient caches excluded; type-specific fields not merely hidden booleans.
- `uobject-metadata`: units, clear tooltips/categories and supported-range explanations.
- `editor-only-module`: no UnrealEd/asset actions in runtime/server module dependencies.
- `async-task-graph`: capture UObjects on GT, immutable value snapshots to workers; this project forbids worker UObject dereferences, stricter than the skill weak-pointer example.
- `agent-investigator` and `graphify`: read-only bounded source evidence; existing graph queried first, direct source verified.

## Open questions

No new owner question is required to begin. The two-asset product direction and permission to drop historical compatibility are explicit. Remaining choices above are technical contracts to implement and document. The scope still includes complete M5, and a valid finite asset is the game's priority; infinite support must remain a real supported streaming mode, not a renamed finite path.
