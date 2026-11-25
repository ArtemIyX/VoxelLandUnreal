# VoxelLand M6 task tracker

**Progress:** 1 of 6 slices verified · **Current:** M6.1 sphere checkpoint · **Pending:** corrected contract fixture rerun

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

- [ ] **M6.2a** Add box, capsule, cut-hole and box-foundation composition. Verify numeric limits, degeneracies, overlap and stable operation ordering.
- [ ] **M6.2b** Decide whether to support smooth blends. Verify any culling against exhaustive evaluation, including downstream halos. Return diagnostics for unsupported settings.
- [ ] **M6.2c** Extend the same semantics to supported erosion-disabled local M4 worlds. Repeat seam, disabled identity, order and resource-limit checks.

### M6.3 — Region filters and accumulated halos

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

### M6.1 execution evidence

UE 5.7.4, Windows x64 Development Editor, headless CPU generation and CPU Surface Nets. Build ran before the initial test and again after the fixture fix. Both builds passed. Automation log `Saved/Logs/VoxelLand-M6.1-Automation.log` in the example project recorded 15 passes and one failure. Further build and test execution is stopped at the owner's request.

```powershell
& 'C:/Users/xeuse/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat' VoxelLandExampleEditor Win64 Development '-Project=C:/Users/xeuse/repos/VoxelLandExample/VoxelLandExample.uproject' -WaitMutex -NoHotReloadFromIDE

& 'C:/Users/xeuse/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'C:/Users/xeuse/repos/VoxelLandExample/VoxelLandExample.uproject' -unattended -nop4 -nullrhi -nosound -nosplash '-ExecCmds=Automation RunTests VoxelLand.M6.Sphere+VoxelLand.M4.Generator.HeightField+VoxelLand.M4.Smooth+VoxelLand.M4.WorldIdentity.Matches+VoxelLand.M3.Filter+VoxelLand.M3.ExternalFilter+VoxelLand.M2.NegativeChunkSeam+VoxelLand.M2.CornerOwnership' '-TestExit=Automation Test Queue Empty' '-abslog=C:/Users/xeuse/repos/VoxelLandExample/Saved/Logs/VoxelLand-M6.1-Automation.log'
```

For each update include the task ID, relevant source or commit, review result, and exact executed command and outcome where applicable. Include UE version, platform and backend with executable evidence.
