# M3 verification handoff

M3 acceptance remains open. The M1 and M2 verification records remain separate and their pending checks still apply. Do not infer a pass from source changes alone. This handoff records authored coverage, not executed results. No build, automation, cook, dedicated-server run, real-RHI preview check, cross-host comparison or performance measurement has been performed for this M3 implementation.

Record the plugin and FastNoise revisions, UE version, target and configuration, platform, CPU, GPU/RHI, selected VM and native backends, seed, input assets and actual results with each run. The native noise backend currently targets SSE2 on supported x64; the scalar VM does not imply a native scalar-noise backend.

## Authored automation

The `VoxelLand.M3` source fixtures cover:

- Graph compilation with stable identity under node/link reordering and unrelated unreachable nodes, duplicate IDs, pin ownership/type errors and cycles; program serialization, stale source and fingerprint rejection; a separately serialized Smooth Density stage and invalid filtered-field fan-out.
- Typed float/int/bool overrides and select, exact int-to-float conversion, a precision-loss error and division-by-zero error.
- The Square provider's scalar result, a missing provider, a version mismatch and rejection of an incompatible provider during reload.
- Scalar versus requested SIMD VM output for all eight delivered Perlin, fBm, Value and Simplex dimensional variants, batches of 1, 2, 3 and 256, negative coordinates and changed world seeds. Native SSE2 or VM SIMD unavailability is reported as a warning; a skipped comparison is not a parity pass.
- Smooth Density against an independently computed integer neighborhood and canonical hash; disabled and zero-strength identity, nonuniform input, the three same-LOD overlap planes at negative chunk coordinates, mid-generation cancellation and concurrent versus serial output. The noise seam fixture checks the three X overlap planes for every delivered family at negative coordinates.

These fixtures have not been built or run. They do not replace independent expected values for every node, cross-host captures, real renderer or server tests. In particular, the user-facing parameter UI, runtime initialization cancellation/stale races, smooth-filter mesh seams and custom-operator scalar/SIMD parity need further coverage. Custom operators now have an opt-in SSE2 interface with exact scalar comparison and fallback; the authored parity and mismatch cases remain unrun. Power now has an authored fixed-order scalar approximation and domain/reference cases, but its numerical accuracy and cross-host parity remain unverified.

## Acceptance checklist

- Create each asset, save, reopen, duplicate, rename and cook. Exercise undo/redo for links, parameters and variadic pins; check stable IDs, links and layout.
- Verify pin and stage rules, duplicate/missing IDs, cycles, output count, invalid settings, unsupported filter chains and missing providers. Errors must identify the affected node/pin where available. Confirm cook failure is enforced and a previous preview remains labeled stale.
- Compare every delivered math and typed-control operation to independent references, including conversion limits, variadic ordering, division and power domains. Exercise defaults and per-world overrides.
- For each noise family, check fixed seeds and 2D/3D positions, negative coordinates, independent node seeds and invalid configuration. Verify provider serialization, version/missing-provider rejection, and scalar/SIMD full batches and tails on each supported backend. Compare Win64 and Linux captures.
- Compare smoothing to an independent integer reference; verify identity, halo, all overlap planes and mesh borders, cancellation, order and concurrency on actual runs.
- With a real RHI, check preview chunks, material and normals; seed/semantic supersession, UI-only and material-only edits, close, PIE and teardown. Record compile, generation, upload, cold shader compilation, p50/p95 and hardware.
- Verify cooked graph/settings load only after explicit runtime initialization. Exercise cancellation and stale rejection, missing provider errors, and a real dedicated-server target without editor, render module, terrain components or GPU resources.

Source-level server guards and module target exclusions are present, but a real server build, cook and process inspection are required to establish them. The settings API is `AVoxelLandWorld::InitializeFromSettings`, with `OnInitializationFinished`, `LastInitializationError`, and `CancelInitialization`; BeginPlay remains inert. The plan's example `.uasset` files are not yet present. NullRHI automation does not establish actual rendering or a dedicated-server path. See [M3_User_Guide.md](M3_User_Guide.md) for usage and limitations.
