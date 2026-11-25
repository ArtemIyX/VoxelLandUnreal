# VoxelLand M7 task tracker

**Progress** 1 of 6 phases verified through source and contract audit. **Implemented** M7.1–M7.2. **Next** M7.3 straight-down seam cover.

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

- [ ] Build deterministic single-owner vertical curtains with constant X/Y, measured depth and a hard cap. Keep separate render vertices, normals, tangents and bounds.
- [ ] Verify 2:1 rim closure and leakage at corners or grazing/upward view angles. Check that terrain frames remain independent of the skirt.
- [ ] Reject missing, vertical, stacked, horizontal-face and depth-cap cases where a vertical curtain cannot cover the gap. Test balanced finer/fallback coverage and budget exhaustion.

## M7.4 — Clipmap, hysteresis and runtime paths

- [ ] Add aligned six-level demand, 2:1 balance, hysteresis, bounded queues/residency and coherent coverage swaps.
- [ ] Wire LOD-aware render grouping, neighbor seam revision keys, cancellation and stale-result rejection.
- [ ] Verify finite and erosion-disabled local M4 with supported M6 operations, full footprints and no render work on the data-only path.

## M7.5 — Acceptance

- [ ] Run the applicable regressions and record revision, engine, platform, exact commands and outcomes.
- [ ] Inspect the 3 km view with grazing lighting, cliffs, foundations and cave-mouth/fallback cases. Exercise boundary jitter, fast travel and teleport. Record cracks/flicker, fallback and resource metrics.
- [ ] Record interactive, cook, actual dedicated-server and cross-host checks individually and keep milestone acceptance open while required evidence is pending.

## Progress log

| Date | Phase | Evidence |
| --- | --- | --- |
| 2026-11-25 | Planning | Tracker initialized on `VOXEL-7` at `5932627`. |
| 2026-11-25 | M7.0 | Audited project `7b936eb` and plugin `5932627`. Graphify traced Surface Nets and finite/local M4/M6 field paths across 339 C++ files, 5,070 nodes and 10,436 edges. Corroborated the integration points in source because Unreal macros and unresolved graph endpoints limit extraction. Recorded numeric footprints, LOD0 compatibility and the finite two-level reference. Static review found no critical issues. No build, test or editor execution. |
| 2026-11-25 | M7.1 | Added ordinary finite LOD1 generation, full-footprint admission, base-lattice compatibility tile reuse, exact stride-two sampling and level spacing in Surface Nets. Actor coarse demand and publication are still gated. Static review found no critical issues. Authored `VoxelLand.M7.Lod.FiniteCoincidence` and `VoxelLand.M7.Lod.MeshSeams`. Build and tests remain unexecuted. |
| 2026-11-25 | M7.2 | Added shared finite M4 normal frames with canonical Q16 anchor fractions, complete tiled base-stencil admission, and exact paired mesh-frame application. Central differences use base spacing, then interpolate raw gradients before normalization. Fallbacks follow the first canonical solid-to-air edge or +Z and are counted. Admission allows up to 1,024 anchors and 256 unique 4³ stencils. Existing actor and terrain topology are unchanged. Paired-frame fixtures use the API ahead of actual rim extraction in M7.3. Static review found no critical issues. Authored `VoxelLand.M7.Normals.FiniteSharedSource` and `VoxelLand.M7.Normals.ReferenceAndPairs`, and extended `VoxelLand.M7.Lod.MeshSeams` with raw and packed frame comparisons. No build or test execution. |

Update the log after each phase with its commit, relevant paths, review findings and executed checks. Leave a checkbox open if its evidence is pending.
