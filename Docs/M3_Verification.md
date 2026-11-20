# M3 verification handoff

M3 acceptance remains open. The M1 and M2 verification records remain separate and their pending checks still apply. Do not infer a pass from source changes alone. The example-asset creation and fresh-process reload checks recorded below were executed; other acceptance areas require their own evidence.

Record the plugin and FastNoise revisions, UE version, target and configuration, platform, CPU, GPU/RHI, selected VM and native backends, seed, input assets and actual results with each run. The native noise backend currently targets SSE2 on supported x64; the scalar VM does not imply a native scalar-noise backend.

## Authored automation

The `VoxelLand.M3` source fixtures cover:

- Graph compilation with stable identity under node/link reordering and unrelated unreachable nodes, duplicate IDs, pin ownership/type errors and cycles; program serialization, stale source and fingerprint rejection; a separately serialized Smooth Density stage and invalid filtered-field fan-out.
- Typed float/int/bool overrides and select, exact int-to-float conversion, a precision-loss error and division-by-zero error.
- The Square provider's scalar result, a missing provider, a version mismatch and rejection of an incompatible provider during reload.
- Scalar versus requested SIMD VM output for all eight delivered Perlin, fBm, Value and Simplex dimensional variants, batches of 1, 2, 3 and 256, negative coordinates and changed world seeds. Native SSE2 or VM SIMD unavailability is reported as a warning; a skipped comparison is not a parity pass.
- Smooth Density against an independently computed integer neighborhood and canonical hash; disabled and zero-strength identity, nonuniform input, the three same-LOD overlap planes at negative chunk coordinates, mid-generation cancellation and concurrent versus serial output. The noise seam fixture checks the three X overlap planes for every delivered family at negative coordinates.

On Windows 11 x64 with UE 5.7 CL 51494982 Development Editor and NullRHI, the M1 suite passed 13/13, M2 passed 10/10, and the latest M3 suite passed 16/16. The M1 capture test explicitly reported that cross-host comparison remains pending. These runs do not replace independent expected values for every node, cross-host captures, real renderer or server tests. User-facing parameter UI, runtime initialization cancellation/stale races, smooth-filter mesh seams and cross-host Power parity still need coverage. Custom operators have an opt-in SSE2 interface with scalar comparison and fallback; Windows automation exercised both the Square provider and the mismatch fallback.

## Acceptance checklist

- Create each asset, save, reopen, duplicate, rename and cook. Exercise undo/redo for links, parameters and variadic pins; check stable IDs, links and layout.
- Verify pin and stage rules, duplicate/missing IDs, cycles, output count, invalid settings, unsupported filter chains and missing providers. Errors must identify the affected node/pin where available. Confirm cook failure is enforced and a previous preview remains labeled stale.
- Compare every delivered math and typed-control operation to independent references, including conversion limits, variadic ordering, division and power domains. Exercise defaults and per-world overrides.
- For each noise family, check fixed seeds and 2D/3D positions, negative coordinates, independent node seeds and invalid configuration. Verify provider serialization, version/missing-provider rejection, and scalar/SIMD full batches and tails on each supported backend. Compare Win64 and Linux captures.
- Compare smoothing to an independent integer reference; verify identity, halo, all overlap planes and mesh borders, cancellation, order and concurrency on actual runs.
- With a real RHI, check preview chunks, material and normals; seed/semantic supersession, UI-only and material-only edits, close, PIE and teardown. Record compile, generation, upload, cold shader compilation, p50/p95 and hardware.
- Verify cooked graph/settings load only after explicit runtime initialization. Exercise cancellation and stale rejection, missing provider errors, and a real dedicated-server target without editor, render module, terrain components or GPU resources.

Source-level server guards and module target exclusions are present, but a real server build, cook and process inspection are required to establish them. The settings API is `AVoxelLandWorld::InitializeFromSettings`, with `OnInitializationFinished`, `LastInitializationError`, and `CancelInitialization`; BeginPlay remains inert. NullRHI automation does not establish actual rendering or a dedicated-server path. See [M3_User_Guide.md](M3_User_Guide.md) for usage and limitations.

## Example asset evidence

On Windows 11 x64, UE 5.7 CL 51494982, Development Editor, NullRHI, the editor build succeeded. The explicitly enabled `VoxelLand.Examples.Create` automation saved the three example assets; `VoxelLand.Examples.Verify` in a fresh editor commandlet reopened them, validated the typed parameters, distinct noise seed slots, smoothing stage and compiled program, and regenerated the same seed-1234 chunk `(0,0,-1)` hash: `11661623748142763676`. The two commandlets exited after their runs. This confirms those save/reload and local CPU-generation checks only; it does not establish real-RHI preview, cook, server, cross-host parity or latency acceptance.

An interactive Win64 real-RHI check subsequently opened `DA_M3_Hills` without dirtying it and showed a continuous terrain surface with the asset-viewer floor and backdrop removed. Changing the transient preview seed from 1234 to 8765 visibly regenerated the surface while the editor still showed **All Saved**. The preview displayed 92.2 ms and 93.6 ms generation times for those two observations; these are individual UI-reported samples, not a latency distribution or GPU upload measurement. After the rendering correction, `VoxelLand.M2` passed 10/10 and `VoxelLand.M3` passed 16/16 in separate NullRHI editor commandlets, including the payload-dirty and preview-floor lifecycle checks. Mirrored transforms, full material and preview interaction coverage, real server, cross-host parity and measured p50/p95 remain open.


## Windows cook evidence

A UE 5.7 Development Editor commandlet cooked the physical plugin `Content/Examples` directory for Windows using `-run=Cook -TargetPlatform=Windows -CookDir=<plugin>/Content/Examples -OutputDir=<project>/Saved/M3CookExamples -NoDefaultMaps -SkipEditorContent -unattended -NullRHI`. It exited 0 with zero reported errors or warnings and produced cooked `.uasset` and `.uexp` files for all three examples. UE 5.7's `-CookDir` expects a filesystem directory; a virtual `/VoxelLand/Examples` path by itself did not include these assets.

`VoxelLand.Examples.CookedProbe`, run separately with `-VoxelLandCookedExampleDir=<cooked>/VoxelLandExample/Plugins/VoxelLand/Content/Examples`, temporarily remounted the cooked plugin content and loaded the cooked world-settings asset and its cooked graph reference, checking their cooked package flags and stripped editor graph. In a transient data-only game world, it verified BeginPlay did not start terrain, cancelled one explicit settings initialization, rejected another after its settings changed, then initialized again and reproduced seed-1234 chunk hash `11661623748142763676`. The probe passed with engine warnings about the two cooked assets' empty engine versions. This is an editor-hosted cooked-data and runtime-API probe; it is not a packaged game or dedicated-server run.

An isolated empty graph was saved under a temporary project-content directory and cooked separately with the same target/options. The cooker logged `Missing density output` and exited 1. It nevertheless wrote partial cooked files to the failed cook output; those files are not a successful cook. The temporary source asset was removed afterward. Logs are `Saved/Logs/M3_CookExamples.log`, `M3_CookedRuntimeFinal.log`, and `M3_CookInvalid.log` in the example project. A source-built or otherwise server-capable engine and a server target are still required for dedicated-server acceptance.
