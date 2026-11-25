# VoxelLand M7 task tracker

**Progress** M7.0 audit and M7.1–M7.4 implementation complete. M7.5 static handoff complete. Milestone acceptance remains open for executable and measured evidence.

See the example project's [M7 technical description](../../../Docs/VoxelLand_M7_Technical_Description.md) and [implementation plan](../../../Docs/VoxelLand_M7_Plan.md).

## M7.0 — Audit and contract

- [x] Record ordinary generation, meshing, normals, request and region publication anchors, LOD0 gates and baseline fixtures.
- [x] Pin LOD ranges, coordinate/precision limits, completed-field identity, exact normals and seam coverage criteria.

| Baseline anchor at `5932627` | Contract input |
| --- | --- |
| `VoxelLandGenerator.cpp:1477`, `:1517`, `:1538`, `:1619` | Ordinary worker dispatch, M4 demand gate, finite footprint validation and bounded raw shaped density tiles. |
| `VoxelLandM4Unlimited.cpp:26`; `VoxelLandM6Baseline.cpp:19`, `:82` | Erosion-disabled local LOD0 admission and the extended M6 baseline path. M7.1 starts with cave-disabled finite M4. |
| `VoxelLandChunkKey.h:7`; `VoxelLandMeshData.h:22`, `:54` | LOD participates in logical key equality and hash. Precision and origin read helpers currently take level spacing separately. |
| `VoxelLandMeshing.cpp:46`, `:106`, `:166`, `:184`, `:199` | Local trilinear gradients, LOD0 gate, deterministic fallback frames, snapped global cell vertices and lower-endpoint quad ownership. |
| `VoxelLandWorld.cpp:94`, `:103`, `:485`, `:711`, `:924`, `:992` | Ordinary worker generation and meshing, LOD0 request and camera demand, stale-result checks, and coordinate-only region publication. |
| `VoxelLand.M2.NegativeChunkSeam`, `VoxelLand.M2.CornerOwnership`, `VoxelLand.M4.Generator.HeightField`, `VoxelLand.M6.Filters.FiniteAndLocalSeams` | Existing seam, owner, finite field and filtered baseline fixtures. Their execution is outside this audit. |

M7.1 uses level spacing `S × 2^L` and samples base-lattice coordinate `(32 × Key.Coordinate + LocalSample) × 2^L`. LOD1 retains base-field density units and the canonical byte hash. Its stored offsets `−1…33` span a bounded 69³ base-lattice window. M4 compatibility smoothing adds one base sample on either end, giving a 71³ raw window. Slope reads add one base sample on either end of XY only. Admission requires finite mesh keys within ±32,766 chunks per level, raw Z within ±2,000,000 cm, native detail XY within ±16,777,216 cm, and the full height-grid footprint. Existing LOD0 fields, hashes, origins, meshes, and public data requests retain their current path. The two-level checkpoint uses an analytic tilted finite field for coincident samples, independent requests at negative coordinates, and same-level face, edge and corner ownership. Mixed-level frame matching and visible transition coverage belong to later phases.

## M7.1 — Two levels through ordinary generation

- [x] Add checked aligned LOD1 sampling and ordinary CPU meshing. Preserve legacy LOD0 density, hashes and mesh behavior when M7 is disabled.
- [ ] Verify coincident samples and same-LOD positions/ownership across faces, edges and corners. Include negative coordinates, limits and request order.

## M7.2 — Edge normals and tangents

- [x] Implement shared base-field derivative sampling and canonical rim normal frames with complete admitted stencil reads.
- [ ] Verify exact same-LOD and paired mixed-LOD normal/tangent frames. Compare an analytic tilted plane and sphere, including saturation/fallback and origin rebasing.

## M7.3 — Straight-down seam cover

- [x] Cut one complete vertical LOD1 face and its four LOD0 neighbors through actual Surface Nets triangles and separate positive-owner candidates. Split both terrain rims at common anchors and apply the shared frames.
- [x] Build a separate coarse-owned vertical curtain with constant X/Y, measured depth and a cap. Preserve prior outputs on unsupported coverage, over-budget requests and cancellation.
- [ ] Execute the mixed-rim fixtures and inspect closure, corners, grazing/upward visibility and terrain/skirt shading. Verify balanced finer fallback and coherent publication after M7.4.

## M7.4 — Clipmap, hysteresis and runtime paths

- [x] Add an opt-in six-level planner with aligned leaves, 2:1 balance, protected hysteresis, a bounded refinement reserve and source admission.
- [x] Stage a complete LOD-keyed render revision through the existing worker queue. Validate and retain sources, seams and replacement components before retiring visible coverage. Keep explicit LOD0 and headless data demand separate.
- [x] Extend ordinary finite and erosion-disabled local M4 coarse sampling and shared frames through the completed field, including supported M6 operations.
- [ ] Execute planner, source, seam and runtime fixtures. Verify interactive transitions and render-frame timing, CPU/GPU resource use, actual server execution, and finite/local M6 cases at the required scene scale.

## M7.5 — Acceptance

- [x] Audit the implementation commits and source entry points. Record the authored fixture manifest, legacy regression scope and evidence still needed.
- [ ] Build the project and run the automation manifest. Record revision, Unreal version, platform, exact commands and outcomes.
- [ ] Measure the 3 km view under grazing, overhead and upward inspection. Check multi-seed finite and local terrain, foundations, cliffs and cave-mouth fallback, camera jitter, fast travel, teleports and world rebasing. Record visible cracks or flicker, publication latency, fallback counts, and CPU/GPU resource peaks.
- [ ] Cook the project and verify packaged renderer behavior.
- [ ] Run on an actual dedicated server and record explicit LOD0 data generation and visual work counts.
- [ ] Compare field and mesh evidence across hosts.

### Static source handoff

Enable `bEnableM7Clipmap` before initializing supported ordinary M4 terrain. `AVoxelLandWorld` captures the opt-in at initialization. Explicit data demand remains LOD0, and data-only worlds do not allocate clipmap state. The six-level path uses a common completed base field for coarse sampling and shared seam frames. For supported vertical mixed faces, clip both rims and shade them from the same logical anchors. Draw the measured straight-down curtain separately. Uniform padded face slabs can prove an empty interface. Unproven horizontal faces, ambiguous rims or incident patches, and inadmissible coarse reads request balanced finer coverage. Exhausted fallback leaves the previously published frontier visible, if one exists, and reports the rejection.

The source audit covers `VoxelLandM7Clipmap.cpp`, `VoxelLandM7CoarseField.cpp`, `VoxelLandM7CompletedField.cpp`, `VoxelLandM7NormalFrames.cpp`, `VoxelLandM7Seams.cpp`, `VoxelLandM7RenderWork.cpp` and `VoxelLandWorldM7.cpp`. The authored setup and capture checklist are in the example project's [M7 implementation plan](../../../Docs/VoxelLand_M7_Plan.md#m75-acceptance-setup).

### Authored M7 automation manifest

| Coverage | Automation names |
| --- | --- |
| Two-level generation and same-level ownership | `VoxelLand.M7.Lod.FiniteCoincidence`, `VoxelLand.M7.Lod.MeshSeams` |
| Shared frame evaluation and source stencils | `VoxelLand.M7.Normals.ReferenceAndPairs`, `VoxelLand.M7.Normals.FiniteSharedSource` |
| Actual mixed-rim geometry and curtains | `VoxelLand.M7.Seams.VerticalTerrain` |
| Aligned demand, face balance, hysteresis and fallback budget | `VoxelLand.M7.Clipmap.Planner` |
| Completed finite/local M4 and supported M6 fields | `VoxelLand.M7.Field.PointwiseAcrossLevels`, `VoxelLand.M7.Field.NeighborhoodPatches` |
| Actor staging, cancellation and data-only isolation | `VoxelLand.M7.Runtime.VisualPublication` |

For legacy regression coverage, include `VoxelLand.M2.NegativeChunkSeam`, `VoxelLand.M2.CornerOwnership`, `VoxelLand.M4.Generator.HeightField`, `VoxelLand.M4.Runtime`, `VoxelLand.M5.Runtime.MeteredM4Generator`, `VoxelLand.M6.Filters.FiniteAndLocalSeams`, `VoxelLand.M6.Redistance.FilteredFiniteAndLocalSeams`, and `VoxelLand.M6.Runtime.AuthoredDataOnly`.

### Evidence status

| Item | Status |
| --- | --- |
| Source, fixture registration and commit audit | Completed statically through implementation revision `e9d5369` |
| Project build and reflected settings compilation | Pending |
| M7 and legacy automation | Authored, pending execution |
| Editor scene and measured 3 km no-cracks/no-flicker outcome | Pending |
| Render-frame timing, publication latency, fallback rates and measured CPU/GPU peaks | Pending |
| Cook and packaged renderer | Pending |
| Actual dedicated server | Pending |
| Cross-host determinism | Pending |

## Progress log

| Date | Phase | Evidence |
| --- | --- | --- |
| 2026-11-25 | Planning | Tracker initialized on `VOXEL-7` at `5932627`. |
| 2026-11-25 | M7.0 | Audited project `7b936eb` and plugin `5932627`. Graphify traced Surface Nets and finite/local M4/M6 field paths across 339 C++ files, 5,070 nodes and 10,436 edges. Corroborated the integration points in source because Unreal macros and unresolved graph endpoints limit extraction. Recorded numeric footprints, LOD0 compatibility and the finite two-level reference. Static review found no critical issues. No build, test or editor execution. |
| 2026-11-25 | M7.1 | Added ordinary finite LOD1 generation, full-footprint admission, base-lattice compatibility tile reuse, exact stride-two sampling and level spacing in Surface Nets. Actor coarse demand and publication are still gated. Static review found no critical issues. Authored `VoxelLand.M7.Lod.FiniteCoincidence` and `VoxelLand.M7.Lod.MeshSeams`. Build and tests remain unexecuted. |
| 2026-11-25 | M7.2 | Added shared finite M4 normal frames with canonical Q16 anchor fractions, complete tiled base-stencil admission, and exact paired mesh-frame application. Central differences use base spacing, then interpolate raw gradients before normalization. Fallbacks follow the first canonical solid-to-air edge or +Z and are counted. Admission allows up to 1,024 anchors and 256 unique 4³ stencils. Existing actor and terrain topology are unchanged. Paired-frame fixtures use the API ahead of actual rim extraction in M7.3. Static review found no critical issues. Authored `VoxelLand.M7.Normals.FiniteSharedSource` and `VoxelLand.M7.Normals.ReferenceAndPairs`, and extended `VoxelLand.M7.Lod.MeshSeams` with raw and packed frame comparisons. No build or test execution. |
| 2026-11-25 | M7.3 | Added optional positive-owner Surface Nets candidates, complete 2:1 X/Y rim clipping, shared knot subdivisions and bounded terrain shading. The separate coarse-owned curtain drops from the upper trace to below the lower, with one coarse voxel maximum depth. Unsupported single-face geometry rejects before publication. Static review found no critical issues. Multiple incident face composition, finer fallback and runtime publication remain M7.4. Authored `VoxelLand.M7.Seams.VerticalTerrain` through ordinary finite generation and real Surface Nets, including both signs, packed frames, four fine participants, nonzero curtain, missing/stacked/empty cases, depth, and cancellation. Build, automation and interactive inspection remain pending. |
| 2026-11-25 | M7.4 | Added the six-level planner, completed-field coincident samples and stencil reads, and opt-in actor visual revisions. The existing worker/ticket queue prepares one replacement and keeps the last published coverage on failure. Data-only requests remain LOD0. The renderer stages up to four leaf records per tick and publishes terrain and curtain components separately. The retained source/clipped/curtain CPU buffer cap is 128 MiB per revision, or 256 MiB across two revisions plus bounded current work and scratch. Matching uniform padded slabs admit empty interfaces. Ambiguous shared-participant clips, incompatible incident curtain intervals, unproven horizontal faces, and rejected coarse reads request balanced finer coverage within the leaf reserve. Authored `VoxelLand.M7.Clipmap.Planner`, `VoxelLand.M7.Field.PointwiseAcrossLevels`, `VoxelLand.M7.Field.NeighborhoodPatches`, and `VoxelLand.M7.Runtime.VisualPublication`. Existing normal and coarse source fixtures now cover the widened source path. Final static review found no remaining critical issues. Build, automation, render-frame timing and interactive acceptance are pending. |
| 2026-11-25 | M7.5 | Audited `498e23d`, `f25d44f`, `49b89eb`, `2ddc047` and `e9d5369` against baseline `5932627`. Verified ancestry with `git merge-base --is-ancestor 5932627 HEAD`, inspected `git log --oneline -6`, and checked `git diff --check 5932627..HEAD`. The plugin worktree was clean at the implementation checkpoint. Confirmed fixture names through source registration declarations. Static handoff complete. Executable and measured acceptance remain open. |

Update the log after each phase with its commit, relevant paths, review findings and executed checks. Leave a checkbox open if its evidence is pending.
