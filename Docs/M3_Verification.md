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

On Windows 11 x64 with UE 5.7 CL 51494982 Development Editor and NullRHI, the M1 suite passed 13/13, M2 passed 10/10, and the latest M3 suite passed 29/29. The M1 capture test explicitly reported that cross-host comparison remains pending. These runs do not replace independent expected values for every node, cross-host captures, real renderer or server tests. Targeted automation covers parameter controls and runtime initialization cancellation/stale rejection; comprehensive interactive parameter UI checks, smooth-filter mesh seams and cross-host Power parity remain open. Custom operators have an opt-in SSE2 interface with scalar comparison and fallback; Windows automation exercised both the Square provider and the mismatch fallback.

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

An interactive Win64 real-RHI check subsequently opened `DA_M3_Hills` without dirtying it and showed a continuous terrain surface with the asset-viewer floor and backdrop removed. Changing the transient preview seed from 1234 to 8765 visibly regenerated the surface while the editor still showed **All Saved**. The preview displayed 92.2 ms and 93.6 ms generation times for those two observations; these are individual UI-reported samples, not a latency distribution or GPU upload measurement. After the rendering correction, `VoxelLand.M2` passed 10/10 and `VoxelLand.M3` passed 16/16 in separate NullRHI editor commandlets, including the payload-dirty and preview-floor lifecycle checks. Mirrored transforms, full material and preview interaction coverage, real server, cross-host parity and end-to-end latency measurements remain open.


## Windows cook evidence

A UE 5.7 Development Editor commandlet cooked the physical plugin `Content/Examples` directory for Windows using `-run=Cook -TargetPlatform=Windows -CookDir=<plugin>/Content/Examples -OutputDir=<project>/Saved/M3CookExamples -NoDefaultMaps -SkipEditorContent -unattended -NullRHI`. It exited 0 with zero reported errors or warnings and produced cooked `.uasset` and `.uexp` files for all three examples. UE 5.7's `-CookDir` expects a filesystem directory; a virtual `/VoxelLand/Examples` path by itself did not include these assets.

`VoxelLand.Examples.CookedProbe`, run separately with `-VoxelLandCookedExampleDir=<cooked>/VoxelLandExample/Plugins/VoxelLand/Content/Examples`, temporarily remounted the cooked plugin content and loaded the cooked world-settings asset and its cooked graph reference, checking their cooked package flags and stripped editor graph. In a transient data-only game world, it verified BeginPlay did not start terrain, cancelled one explicit settings initialization, rejected another after its settings changed, then initialized again and reproduced seed-1234 chunk hash `11661623748142763676`. The probe passed with engine warnings about the two cooked assets' empty engine versions. This is an editor-hosted cooked-data and runtime-API probe; it is not a packaged game or dedicated-server run.

An isolated empty graph was saved under a temporary project-content directory and cooked separately with the same target/options. The cooker logged `Missing density output` and exited 1. It nevertheless wrote partial cooked files to the failed cook output; those files are not a successful cook. The temporary source asset was removed afterward. Logs are `Saved/Logs/M3_CookExamples.log`, `M3_CookedRuntimeFinal.log`, and `M3_CookInvalid.log` in the example project. A source-built or otherwise server-capable engine and a server target are still required for dedicated-server acceptance.

## Compiler optimization and semantic version

On Windows with UE 5.7 Development Editor, a later build and NullRHI automation exercised the deterministic register allocator, constant compaction, rejection of stale semantic versions, a 600-node varying graph with more than 1024 virtual registers, a folded graph, and preservation of a divide-by-zero error. The M1 suite and M3 suite passed; the final added domain regression passed separately. The example graph was resaved with the new version, reopened successfully, cooked for Windows, and passed the editor-hosted cooked-settings/runtime probe. The first cooked probe invocation supplied an incorrect directory and failed; retrying with the actual output path passed. These runs do not establish cross-platform or dedicated-server acceptance.

## External filter stage

On Windows UE 5.7 Development Editor and NullRHI, `VoxelLand.M3.ExternalFilter` compared the example filter against an independent integer reference over a negative-coordinate chunk, checked all three overlap axes and cancellation without partial publication. `VoxelLand.M3.ExternalFilterGraph` checked stage compilation, frozen provider leases, serialization, and chain, fanout and missing-version rejection. These checks exercise a translation-invariant scalar filter; custom provider implementations, interactive editor wiring, Linux and dedicated-server execution require their own verification.

## Checked external outputs

`VoxelLand.M3.CheckedOperatorOutput` exercises all int16 density bit patterns, int32 and finite-float extremes, both bool values, invalid/duplicate/out-of-range writes, scalar short writes at batch sizes 1, 2, 3, 4, 255 and 256, and concurrent short-write filter failures. It does not establish safety against arbitrary native memory corruption by a malicious binary.

## Measured real-RHI preview

Two interactive Win64 UE 5.7 CL 51494982 sessions used a Ryzen 9 9950X3D and RTX 5080 with D3D12/SM6. The saved `DA_M3_Hills` graph ran at preview radius 1/1 (18 chunks), with no overrides or selected settings/material. Across 21 confirmed seed samples (1001 and 1004–1023), the UI-reported generation times in milliseconds were: 94.5, 93.9, 95.1, 92.8, 95.1, 94.8, 95.0, 92.8, 92.9, 95.2, 94.9, 94.0, 94.4, 93.7, 93.6, 94.8, 93.9, 95.4, 94.4, 95.2, 94.1. Median generation was 94.4 ms and nearest-rank p95 was 95.2 ms. The UI reported 0.6–0.7 ms game-thread setup and 0.1 ms compilation. These are the reported components, not end-to-end interaction latency, GPU upload timing or cold shader timing; no p50/p95 was measured for those.

## Cooked C++ provider fixtures

Opt-in automation `VoxelLand.ProviderCook.Create` uses a caller-supplied unique `/Game/__VoxelLandM3ProviderProbe_*` root and refuses to overwrite existing packages. A scoped Windows cook of isolated sample-provider and one-cell-filter graphs succeeded. The editor-hosted `VoxelLand.ProviderCook.CookedProbe` remounted cooked project content, verified both graphs had editor data stripped, loaded and executed each provider payload, and rejected missing providers using an empty immutable registry snapshot and incompatible provider versions. Cooked asset load emitted Unreal's empty-engine-version warnings. These tests do not establish a packaged game, real dedicated-server build, or third-party provider compatibility.

## Structured graph diagnostics

Windows UE 5.7 editor automation exercises data-layer and compiler diagnostics for invalid pins and links, root filter fanout and strength, missing providers, constant division by zero, and ambiguous duplicate IDs. The editor focus resolver was checked for exact pin targets and safe node-only fallback, including an invalid editor literal. These are automated target-resolution checks; visually focusing the pin in an interactive graph editor was not rechecked in this run.

## Bounds and reload lifecycle

The runtime scheduler uses conservative interval proofs for a supported subset of pure instructions to avoid the post-sampling surface scan for provably empty or solid chunks. It still generates full density data and preserves evaluation errors. Bounds expand by one cell for active Smooth Density; external filters, unsupported operations, uncertain signs and small negative values near quantization zero return unknown. Windows NullRHI automation compared proven classifications against generated quantized chunks, including active smoothing and a near-zero negative case. Cycle diagnostics use a deterministic actual cycle edge. Simulated package reload and object replacement tests exercised preview work draining and cancellation of pending runtime initialization; interactive reload/reinstancing and dedicated-server operation remain unverified.

## Latest verification record

The latest Windows UE 5.7 CL 51494982 Development Editor build passed. NullRHI automation passed M1 13/13, M2 10/10 and M3 29/29 (logs `VoxelLandM1CheckedOutputFinal.log`, `VoxelLandM2BoundsReloadFinal.log` and `VoxelLandM3BoundsReloadFinal.log`). The M1 and M2 suites ran in earlier revisions of the same pending work; the latest M3 run includes interval, diagnostics, provider and reload tests. Windows cooks and editor-hosted probes for the example graph and both custom-provider stages passed as recorded above. The provider ABI now requires `RegisterV2` and checked output writers; third-party binaries using the old registration export must be rebuilt. Dedicated-server and Linux builds, cross-host parity, full interactive reload and packaged-game behavior remain unverified.

## VM-SIMD-disabled Windows run

With `VOXELLAND_ENABLE_VM_SIMD=0` temporarily applied to the UE 5.7 Win64 Development Editor build, M3 passed 29/29 in NullRHI. The suite reported that forced VM SIMD parity and the persistent SIMD mismatch fallback could not be exercised in this configuration; these are not SIMD parity passes. M1 passed 13/13 with the explicit scalar-VM expectation. A separate noise capture reported scalar VM and native SSE2; after restoring the original build file and rebuilding normally, the Windows SIMD4 VM/native SSE2 run matched that capture on the same host. The Linux and cross-host checks remain open. See [M1_Verification.md](M1_Verification.md) for revisions and logs.
