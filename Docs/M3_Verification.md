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

Windows UE 5.7 editor runs passed M1 13/13 and M2 10/10 at the recorded revisions. A later full NullRHI M3 run reported 33 successful entries, including two explicit native-render skips, so 31 tests were exercised; subsequent math, shape, filtered-mesh, active-preview and real PIE checks ran separately as documented below. Smooth-filter mesh seams and slice-stage cancellation were checked on same-LOD fixtures. The M1 cross-host comparison, Linux and dedicated-server execution, GPU upload/presentation and cold-shader timing, and comprehensive interactive parameter UI checks remain open. Custom operator scalar and SIMD fallback behavior was exercised on Windows.

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

## Earlier verification record

An earlier Windows UE 5.7 CL 51494982 Development Editor build passed. NullRHI automation passed M1 13/13, M2 10/10 and M3 29/29 (logs `VoxelLandM1CheckedOutputFinal.log`, `VoxelLandM2BoundsReloadFinal.log` and `VoxelLandM3BoundsReloadFinal.log`). The M1 and M2 suites ran in earlier revisions of the same pending work; that M3 run includes interval, diagnostics, provider and reload tests. Windows cooks and editor-hosted probes for the example graph and both custom-provider stages passed as recorded above. The provider ABI now requires `RegisterV2` and checked output writers; third-party binaries using the old registration export must be rebuilt. Dedicated-server and Linux builds, cross-host parity, full interactive reload and packaged-game behavior remain unverified.

## VM-SIMD-disabled Windows run

With `VOXELLAND_ENABLE_VM_SIMD=0` temporarily applied to the UE 5.7 Win64 Development Editor build, M3 passed 29/29 in NullRHI. The suite reported that forced VM SIMD parity and the persistent SIMD mismatch fallback could not be exercised in this configuration; these are not SIMD parity passes. M1 passed 13/13 with the explicit scalar-VM expectation. A separate noise capture reported scalar VM and native SSE2; after restoring the original build file and rebuilding normally, the Windows SIMD4 VM/native SSE2 run matched that capture on the same host. The Linux and cross-host checks remain open. See [M1_Verification.md](M1_Verification.md) for revisions and logs.


## Real package reload on Windows

UE 5.7 CL 51494982 Win64 Development Editor, normal RHI: `VoxelLand.M3.Editor.RealPackageReload` passed 1/1. It saved uniquely named copies, called Unreal's real `ReloadPackage`, and verified that a material-setup reference was repointed without starting a density job; the original toolkit stayed usable. With a compile result pending, a started tracked worker was held at a barrier until a separate coordinator observed reload entering its drain; reload remained blocked until release, then joined the worker, invalidated the old result and stopped the original toolkit. Unreal reopened an editor for the replacement graph. The fixture closed the reopened editor, reset loaders and removed its files. The actual preview job reached a checkpoint after creating its generator and worker, and the actual settings-initialization job reached a checkpoint after loading its program. Bounded test-only barriers held both until real graph reload cancelled their tokens; both observed cancellation, and the actor published no world state. This proves those checkpoints, not cancellation at every possible instruction or during an individual chunk evaluation. The real-RHI log is `Saved/Logs/M3_ActiveReloadFinal.log`; the editor build is `Saved/Logs/M3_ActiveReloadBuildFinal.log`.

An earlier NullRHI attempt hit Unreal's native-window `FGenericWindow::GetRestoredDimensions` fatal while closing the automatically reopened editor; the fixture now explicitly skips with a warning when rendering is unavailable. Failed-run temporary files were removed after that editor process exited. Full interactive reload behavior remains unverified.


On the same final build, NullRHI editor automation completed M2 10/10 and reported M3 30/30 successful entries, with the real package reload entry explicitly skipped with a native-window warning; the other 29 M3 tests ran. The independent real-RHI reload run above exercised that entry. Both commandlets exited with code 0. Logs: `Saved/Logs/M2_ActiveReloadFinal.log` and `Saved/Logs/M3_ActiveReloadSuite.log`.


## Preview render-command timing

On Windows 11 x64, UE 5.7 CL 51494982 Development Editor, Ryzen 9 9950X3D and RTX 5080 with D3D12/SM6, the real-RHI `VoxelLand.M3.Editor.PreviewTiming` automation passed, including superseding and stopping while a render fence was pending. It measured 20 sequential seed changes on a transient copy of `DA_M3_Hills`, radius 1/1 (18 nonempty chunks), default material and no overrides. A preview scene was constructed without an interactive viewport; samples are warm, not cold-shader or first-frame measurements.

| Timing (ms) | Median | p95, nearest rank |
| --- | ---: | ---: |
| CPU generation | 91.2 | 94.5 |
| Game-thread component setup | 0.6 | 0.7 |
| Setup to render-fence observation | 49.2 | 49.6 |
| Seed change to render-fence observation | 141.9 | 191.9 |

The latest compile CPU time displayed was 0.1 ms, from the initial compile rather than the subsequent seed-only changes. The 50 ms ticker quantizes fence observations; these are observation latencies, not exact render-thread execution durations. `FRenderCommandFence` at render-thread depth covers prior queued render commands but does not measure GPU completion, actual upload duration, displayed pixels or shader compilation. No GPU upload, frame presentation or cold-shader p50/p95 is claimed. `Saved/Logs/M3_PreviewTimingFinal.log` contains the samples and `Saved/Logs/M3_PreviewTimingBuildFinal.log` the build.

The final build�s NullRHI M3 suite reported 31 successful entries, with `PreviewTiming` and `RealPackageReload` explicitly skipped due to their native-render/window requirement; the other 29 ran. Both skipped entries were exercised separately under real RHI as recorded above. Log: `Saved/Logs/M3_PreviewTimingNullRHI.log`.

After adding the explicit Ready status, the editor rebuilt successfully (`Saved/Logs/M3_PreviewReadyBuild.log`). The real-RHI timing test passed again with 20 samples (`Saved/Logs/M3_PreviewReadyRealRHI.log`): generation median/p95 91.05/92.4 ms, setup 0.6/0.7 ms, fence observation 49.25/49.6 ms, seed change to fence observation 141.7/191.6 ms. The same caveats above apply. NullRHI editor tests reported 11 successful entries with PreviewTiming and RealPackageReload skipped; the other nine ran (`Saved/Logs/M3_PreviewReadyEditorNull.log`).


## Preview terrain framing

On UE 5.7 Win64 Development Editor with D3D12, the `DA_M3_Hills` preview was checked interactively: Frame terrain and F in the focused preview each moved the camera to the published terrain, and the padded frame kept the sample visible in the tested narrow pane. `VoxelLand.M3.Editor.PreviewTiming` also checks empty, published, stale and stopped framing states and the aggregate bounds of the published components. Build: `Saved/Logs/M3_FrameTerrainBuildFinal2.log`; real-RHI automation: `Saved/Logs/M3_FrameTerrainFinal2.log`.

NullRHI `VoxelLand.M3.Editor` reported 11 successful entries, with `PreviewTiming` and `RealPackageReload` explicitly skipped; the other nine ran. Log: `Saved/Logs/M3_FrameTerrainNull.log`.


## Editor asset and graph workflows

UE 5.7 Win64 Development Editor real-RHI `VoxelLand.M3.Editor` passed 13/13 (`Saved/Logs/M3_EditorAssetFactoryRealRHI.log`; build `M3_EditorAssetFactoryBuild.log`). The isolated fixture invokes AssetTools factories for all three asset types, then separately saves example-derived graph, settings and material assets, duplicates and renames each with AssetTools, and reloads both original and renamed packages. It verifies graph node IDs, pin IDs, exact links and layout, settings references/values, material setup version/reference, and saved-versus-edited graph dirty state. The factory-created blank assets are not the assets saved and reopened. All generated paths are unique and checked for prior files; fixture files were removed.

The graph-editing test exercises link connection undo/redo, wired variadic reordering/removal, and clipboard copy/paste with fresh node/pin IDs and preserved internal links. Existing named-override tests exercise declaration rename/type changes and details refresh. The prior NullRHI full M3 run on the preceding build reported 33 successful entries, two of which were explicit native-render skips (31 exercised; `Saved/Logs/M3_EditorWorkflowNullFinal.log`); the subsequent factory addition was validated in the real-RHI editor suite above.

The subsequent cleanup audit added scoped asset-registry removal and package collection without resetting the editor transaction buffer or clearing selection. A real-RHI `AssetWorkflow` rerun passed immediate and one-second delayed checks that the unique packages were unloaded and their registry entries absent (`Saved/Logs/M3_AssetCleanup3.log`; build `M3_AssetCleanupBuild3.log`). Unreal logged six AssetRegistry warnings while processing queued file-change notifications for the deleted fixture files; the delayed absence checks passed. No fixture files remained after exit.


## Independent math and typed-control reference

UE 5.7 Win64 Development Editor NullRHI `VoxelLand.M3.ReferenceOperations` passed (`Saved/Logs/M3_ReferenceOpsFinal3.log`; build `M3_ReferenceOpsBuildFinal3.log`). Graphs use position-derived inputs to avoid constant folding and compare quantized output against independent arithmetic and truth-table references. Coverage includes Add, Subtract, Multiply, Divide, Min, Max, Abs, Negate, Clamp and Saturate endpoints, five Lerp interpolation/extrapolation factors, all three float comparisons, boolean And/Or/Not, and float/int/bool selects with both conditions. Counts 1, 2, 3 and 256 were checked with scalar, automatic and forced SIMD4 selections; the observed selections were scalar, SIMD4 and SIMD4 respectively. Typed opcodes still use their scalar execution path even with the SIMD4 VM backend selected; this is not a separate typed SIMD implementation. Constant and per-sample invalid clamp/divide domains returned errors, and the latter published no partial samples. An initial test attempt incorrectly compared raw float values to quantized outputs; the corrected oracle uses voxel size 1024 and independent ties-away rounding. This fixture does not establish reference coverage for every shape/CSG operation or cross-platform equality.


## Independent shape and CSG reference

UE 5.7 Win64 Development Editor NullRHI `VoxelLand.M3.ReferenceShapes` passed (`Saved/Logs/M3_ReferenceShapesFinal.log`; build `M3_ReferenceShapesBuildFinal.log`). Position-dependent graph programs compare quantized outputs against independent plane, sphere and box distance formulas, including negative coordinates, axis boundaries and sphere/box off-axis samples, and against independent Union, Subtract and Intersect formulas. Counts 1, 2, 3 and 256 used scalar, automatic and forced SIMD4 selections; observed selections were scalar, SIMD4 and SIMD4. Dynamic shape opcodes use their scalar execution path even with the SIMD4 VM selected. Invalid constant radii, box extents and plane normal fail compilation; dynamic invalid parameters produce a math-domain error with no partial output. Dynamic positive sphere radius and box extent were checked through the sample immediately before their zero boundary. This is a finite, same-host reference sample, not exhaustive geometry or cross-platform verification.

The CSG fixture was strengthened to use A = X and B = -2X - 2, with an explicit assertion that each operand wins for each operation across the sampled range. The focused test passed again (`Saved/Logs/M3_ReferenceCsgBranches.log`; build `M3_ReferenceCsgBranchesBuild.log`).


## Filtered mesh seams and slice cancellation

UE 5.7 Win64 Development Editor NullRHI `VoxelLand.M3.Filter*` passed 6/6 (`Saved/Logs/M3_FilterMesh.log`; build `M3_FilterMeshBuild.log`). The new seam fixture generates smoothed negative-coordinate neighboring chunks on all three axes, meshes them through Surface Nets and compares nonzero shared world-space vertices and normals in both directions. A separate deterministic cancellation case cancels at poll 203, after the 198 expanded-density batches and during the smoothing slices, verifies that no partial chunk is published, then reuses the same worker and compares its completed chunk and hash against the independent integer reference. The completed run asserts the current 235-poll schedule (198 batches, 35 slices and two final polls); the stage determination depends on that schedule. These checks cover the tested same-LOD fixture, not all possible meshes or mixed-resolution seams.


## Active preview supersession and teardown

UE 5.7 Win64 Development Editor real-RHI `VoxelLand.M3.Editor.ActivePreviewLifecycle` passed (`Saved/Logs/M3_ActivePreviewFinal.log`; build `M3_ActivePreviewBuildFinal.log`). Test-only bounded barriers held actual preview workers after generator creation. A newer seed cancelled the first worker, the old result never published, and only the replacement seed published. A second held worker observed cancellation when the toolkit's PIE-begin handler was invoked; its PIE-end handler then resumed and published the current seed once. Stopping during a third held worker cancelled it, drained work and produced no later publication. The test invokes toolkit PIE handlers directly in an editor-hosted automation process; it does not start a complete interactive PIE session. The checkpoint precedes individual chunk generation, so other cancellation timings are not exhaustively established.


## Isolated real PIE lifecycle

UE 5.7 Win64 Development Editor real-RHI `VoxelLand.M3.Editor.IsolatedPIELifecycle` passed 1/1 with `-VoxelLandIsolatedPIE -unattended` in a separately launched process (`Saved/Logs/M3_RealPIEFinal.log`; build `M3_RealPIEBuildFinal.log`). The test creates a new in-memory editor map and transient graph copy, holds an actual preview worker after generator creation, starts an in-process PIE session, and confirms the real PreBeginPIE delegate cancels and drains it without stale publication. Unreal logged creation of a `/Temp/UEDPIE` world. The test ends PIE, observes EndPIE resumption and one current-seed preview publication. It uses a bounded teardown deadline and the launched editor exited 0. This opt-in test must run only in an isolated unattended editor process because it creates a new editor map. The checkpoint is before chunk generation and is not exhaustive cancellation timing.

Without the opt-in flag under NullRHI, the test explicitly skipped with a warning (`Saved/Logs/M3_RealPIE_OptOut.log`); the prior active-preview regression passed with real RHI after delegate registration (`Saved/Logs/M3_RealPIE_ActiveRegression.log`).


## Native noise assembly and wiring reference

UE 5.7 Win64 Development Editor NullRHI `VoxelLand.M3.NativeNoiseOracle` passed (`Saved/Logs/M3_NativeOracleFinal2.log`; build `M3_NativeOracleBuildFinal2.log`). For each delivered Perlin, fBm, Value and Simplex 2D/3D form, graph-compiled output from two independently seeded nodes was compared with separately constructed FastNoise native instances using fixed precomputed seed bits, permuted coordinate axes, distinct frequencies and nondefault fBm octave/gain/lacunarity settings. The comparison independently rounds the combined output at voxel size 1024. Two world seeds and sample counts 1, 2, 3 and 256 were exercised for scalar, automatic and forced SIMD4 VM selections; observed VM selections were scalar, SIMD4 and SIMD4, with native SSE2 in each. Native reference instances were created, sampled and destroyed on their owning worker thread. The uncooked test module has a private FastNoiseRuntime dependency solely for this reference fixture.

The two paths share FastNoise and its native construction API, so this is a wiring and configuration oracle, not an independent implementation of the underlying noise algorithms or a Linux/cross-host golden. Unsupported native or VM backends must be reported as unverified, not parity passes.


## Noise diagnostic focus correction

Noise frequency, octave, gain and lacunarity defaults are now checked while compiling graph nodes, so invalid literal or parameter-backed defaults identify the affected node and input pin rather than falling through to an unlocated program-validation error. The program validator remains in place for external or hand-authored drafts. UE 5.7 Win64 Development Editor built successfully (`Saved/Logs/M3_NoiseFocusBuild.log`); focused NullRHI compiler and asynchronous editor focus-resolution tests passed 2/2 (`Saved/Logs/M3_NoiseFocusTests.log`). A full NullRHI M3 run reported 42 successful entries, four of which explicitly skipped native-render or opt-in PIE checks (38 exercised; `Saved/Logs/M3_NoiseFocusFull.log`). It also logged six known AssetRegistry fixture cleanup file-change warnings. The editor automation invokes focus after resolving the node and pin; visual movement for this correction requires a separate interactive recheck.


The subsequent real-RHI interactive recheck placed the invalid fBm frequency node offscreen. Focus error moved the graph to that node and outlined it orange; the frequency pin was visible but was not separately highlighted. The invalid frequency retained the previous terrain with an error/stale status, and undo restored Ready and All Saved. In the same interactive work, transient Amplitude 700 to 1200, Octaves 4 to 2 and SmoothingEnabled true to false were accepted, the preview returned Ready and the asset stayed All Saved; the amplitude change visibly altered the terrain. Material and world-settings selection and clearing were confirmed in an earlier interactive session, not this recheck.
