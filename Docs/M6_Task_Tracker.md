# VoxelLand M6 task tracker

**Progress:** 1 of 6 slices verified · **Implemented:** M6.1–M6.3 · **Next implementation:** M6.4 bounded re-distance

M6 adds generation-time stamps and filters to cave-disabled M4 terrain. The first checkpoint is one hard sphere stamp across real finite M4 chunk seams. M6 acceptance also requires the remaining stamps, filters with accumulated halos, and bounded re-distance. M5 remains open and deferred. Runtime terrain editing is out of v1 scope.

See the example project's [M6 plan](../../../Docs/VoxelLand_M6_Plan.md), [technical description](../../../Docs/VoxelLand_M6_Technical_Description.md), and [canonical milestone criteria](../../../Docs/Voxel_Terrain_System.md#25-implementation-plan).

## Checklist

### M6.0 — Source audit and sphere contract

- [x] **M6.0a** Audit the finite and erosion-disabled local M4 generation paths, existing primitives and smoothing, CPU mesher, and M3/M4 fixtures. Record source anchors and both Git revisions. Preserve the three untracked M5 field-audit drafts.
- [x] **M6.0b** Pin the sphere sign, units, quantization, stage order, bounds, identity, budgets and cancellation. Identify immutable input ownership, the empty-list bypass, an independent oracle and runnable regression commands.

### M6.1 — First checkpoint across real M4 chunks

- [x] **M6.1a** Add a validated, versioned, immutable sphere stamp and ordered list. Apply hard add and subtract on global sample coordinates in the ordinary cave-disabled finite generator, using bounded exhaustive evaluation.
- [ ] **M6.1b** Verify analytic signs and boundaries, empty/disabled M4 density and hash equality, and failure or cancellation without partial output.
- [x] **M6.1c** Compare full stored overlaps and CPU mesh seam ownership and positions across X/Y/Z, negative coordinates and a corner. Repeat with reversed chunk request order.

### M6.2 — Remaining stamps and local generation

Implementation and static review complete. The following items remain open for executable verification.

- [ ] **M6.2a** Add box, capsule, cut-hole and box-foundation composition. Verify numeric limits, degeneracies, overlap and stable operation ordering.
- [ ] **M6.2b** Decide whether to support smooth blends. Verify any culling against exhaustive evaluation, including downstream halos. Return diagnostics for unsupported settings.
- [ ] **M6.2c** Extend the same semantics to supported erosion-disabled local M4 worlds. Repeat seam, disabled identity, order and resource-limit checks.

### M6.3 — Region filters and accumulated halos

Implementation and static review complete. The following items remain open for executable verification.

- [ ] **M6.3a** Implement region-limited smoothing and the agreed flatten and make-space behavior. Define masks, rounding, order and interaction with existing M4 smoothing.
- [ ] **M6.3b** Compute the full read footprint from stored offsets −1…33, summed sequential filter radii, and relevant M4 and mesher stencils. Reject requests beyond the cap or source domain.
- [ ] **M6.3c** Verify sequential filters, region edges, negative coordinates, all axes and corners. Compare full density overlaps and CPU mesh seams across chunk request orders.

### M6.4 — Bounded re-distance

- [ ] **M6.4a** Define and implement deterministic narrow-band behavior with a documented error metric, halo, limits and cancellation.
- [ ] **M6.4b** Measure error against analytic sphere, plane and box references and filter-altered fields. Verify band and halo boundaries, adjacent chunk consistency and explicit rejection of unsupported requests.

### M6.5 — Authoring and milestone acceptance

- [ ] **M6.5a** Expose supported generation-time controls and diagnostics. Ship a small foundation or clearing example.
- [ ] **M6.5b** Run focused M6 and relevant M3/M4 regressions. Verify seam-free density and CPU meshes on supported finite and local paths, deterministic request ordering and headless CPU generation.
- [ ] **M6.5c** Record cook, real dedicated-server, cross-host and visual checks individually as passed, failed or pending. Review the milestone evidence against the canonical criteria.

## Progress updates

Update this tracker after each task. Check a box after its deliverable and any required verification are complete. Update **Progress** and **Next** when a slice closes. Keep failed or unrun checks open and record the reason in the log. Keep the example project's plan delivery log in sync at each slice handoff.

| Date | Task | Result and evidence |
| --- | --- | --- |
| 2026-11-25 | M6.0 | Audited the project at `7b936eb` and plugin at `e3635e6`. Pinned the finite sphere contract in the technical description, including exact Q8 distance, stage order, limits, identity and publication. |
| 2026-11-25 | M6.1 | Added finite generation-time sphere add/subtract, immutable list ownership and canonical payload identity. Static review found no critical issues. The first automation run passed the real finite density and CPU-mesh seam test plus 14 M2–M4 regressions. The contract test failed on its operation-byte assertion. Its fixture now reads byte 28, and the incremental build passed. The corrected contract test remains pending. |

| 2026-11-25 | M6.2 | Added hard sphere, box and capsule stamps, ordered cut-hole/foundation composition, and the same once-only stamp stage on the ordinary erosion-disabled local path. Smooth blends are explicitly rejected. Static review found no critical issues. Authored `VoxelLand.M6.Stamps.ShapesAndFoundation` and `VoxelLand.M6.Stamps.LocalAcrossSeam`. Build and tests were not run at the owner's request. |

| 2026-11-25 | M6.3 | Added ordered box-masked smooth, flatten and make-space stages with backward lattice demand planning. Reused the existing M4 compatibility smooth kernel and local fixed-lattice baseline path. Static review found no critical issues. Authored `VoxelLand.M6.Filters.Contract` and `VoxelLand.M6.Filters.FiniteAndLocalSeams`. Build and tests were not run at the owner's request. |

### M6.3 filter footprint

The planner uses ordered masks and skips a stage if its mask contains no lattice samples in the stage's demanded box. Active neighborhood stages expand the earlier input by one voxel. Each chunk admits at most eight filter stages and eight voxels of accumulated M6 halo. The largest baseline field is 51³ samples, with separate M4 compatibility smoothing and slope reads. The workload gate admits up to 40 million logical stamp/stencil and baseline units, and 2 MiB of logical filter and field scratch per chunk. Existing worker/provider resources remain governed by M4. Make-space clears only inside its box, with an inclusive mask and hard field subtraction.

### M6.2 arithmetic and ownership

Generic stamps admit complete shape bounds within ±2^45 cm, dimensions up to 2,000,000 cm, and capsule endpoint spans up to 2,000,000 cm per axis. Voxel sizes are integral and at most 134,217 cm. A sample at least 32 voxels outside the enclosing bounds on any axis is already a saturated hard-CSG identity. The remaining maximum per-axis distance before Q8 conversion is 8,294,944 cm, which bounds the three-axis Q8 square sum within uint64. Capsule projection rounds its Q32 parameter using two 16-bit quotient steps and rounds the resulting closest-point offset to Q8. The temporary local M4 generator receives baseline settings only. One fixed 35³ int16 copy lets the parent apply stamps and publish the final hash without modifying the baseline.

### M6.1 execution evidence

UE 5.7.4, Windows x64 Development Editor, headless CPU generation and CPU Surface Nets. Build ran before the initial test and again after the fixture fix. Both builds passed. Automation log `Saved/Logs/VoxelLand-M6.1-Automation.log` in the example project recorded 15 passes and one failure. Further build and test execution is stopped at the owner's request.

```powershell
& 'C:/Users/xeuse/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat' VoxelLandExampleEditor Win64 Development '-Project=C:/Users/xeuse/repos/VoxelLandExample/VoxelLandExample.uproject' -WaitMutex -NoHotReloadFromIDE

& 'C:/Users/xeuse/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/Users/xeuse/repos/VoxelLandExample/VoxelLandExample.uproject' -unattended -nop4 -nullrhi -nosound -nosplash '-ExecCmds=Automation RunTests VoxelLand.M6.Sphere+VoxelLand.M4.Generator.HeightField+VoxelLand.M4.Smooth+VoxelLand.M4.WorldIdentity.Matches+VoxelLand.M3.Filter+VoxelLand.M3.ExternalFilter+VoxelLand.M2.NegativeChunkSeam+VoxelLand.M2.CornerOwnership' '-TestExit=Automation Test Queue Empty' '-abslog=C:/Users/xeuse/repos/VoxelLandExample/Saved/Logs/VoxelLand-M6.1-Automation.log'
```

For each update include the task ID, relevant source or commit, review result, and exact executed command and outcome where applicable. Include UE version, platform and backend with executable evidence.
