# M3 verification handoff

M3 acceptance is **not established** by this document. No build, automation, cook, dedicated-server run, real-RHI preview check, cross-host comparison or performance measurement was performed for this handoff. The M1 and M2 verification records remain separate and their outstanding checks still apply.

Record the plugin and dependency revisions, UE version, target and configuration, platform, CPU, GPU/RHI, seed and test data with each result. Record failures and missing capabilities rather than treating an unrun check as a pass.

## Acceptance checklist

- Create graph, world-settings and material-setup assets; save, reopen, duplicate, rename and cook them. Exercise undo and redo for connections, parameters and variadic pins; check stable IDs, links and layout.
- Verify typed pin and stage rules, duplicate and missing IDs, cycles, output count, invalid values, unsupported filters and missing providers. Confirm diagnostics identify the affected node or pin, invalid graphs cannot cook or start, and an old preview is visibly stale.
- Compare every delivered math and typed-control operation against independent references, including conversion limits, variadic ordering, division by zero and power domains. Exercise float, int and bool defaults and overrides.
- For each delivered noise family, check fixed seeds and 2D/3D positions, negative coordinates, independent node seeds and invalid configuration. Verify provider version and missing-provider failures, serialization and reload, and scalar/SIMD parity for full batches and tails where supported. Native FastNoise remains SSE2-based; a scalar VM does not establish a scalar noise backend.
- Compare Smooth Density against an independent integer reference, including disabled and zero-strength identity, negative coordinates, halo, three overlap planes, mesh borders, cancellation and different generation orders and worker counts.
- In a real RHI preview, check multiple chunks, material and normals. Confirm seed and semantic changes supersede old work, layout changes do not compile, material changes do not regenerate density, and completion after close, PIE or teardown cannot publish. Record compile, generation and upload times separately, along with cold shader compilation, hardware and p50/p95 latency.
- Load cooked settings and graph in runtime and confirm generation begins only after explicit initialization. Check missing providers fail visibly. Run a dedicated-server target and inspect its loaded dependencies and behavior for editor, preview and GPU independence. Compare supported Win64 and Linux outputs before claiming cross-host determinism.

The example graph and assets described in the [M3 plan](../../../Docs/VoxelLand_M3_Plan.md) must be exercised as delivered; their design in the plan alone does not establish that they exist or work. NullRHI automation does not prove rendering or dedicated-server behavior.


## Current source-level entry points

The preliminary graph asset stores versioned node, pin, link and typed parameter records. Node records include bounded operator-specific configuration bytes and seed salt; these are preliminary and no provider validates or canonicalizes them. The graph has no compiled payload or canonical source identity, cannot initialize terrain, and does not yet block cooking. CaptureSnapshot copies records and parameters for worker use after structural validation, but is not a compiler or semantic validation. World settings contain a seed, bounds, typed overrides and a soft material-setup reference; `ValidateSettings` checks IDs, ASCII parameter names, types, values and ranges. The material setup stores a soft material reference. These are source-data foundations, not a graph editor, compiler, preview or runtime initialization workflow.

`FVoxelLandGenerator::GenerateSmoothedChunkCancellable` is an explicit C++ operation on an existing generator and worker. It is not connected to graph assets or world streaming. It samples the 37³ expanded global lattice, quantizes the source, averages 27 neighbors in Z/Y/X order and blends using a strength rounded to a 0–65535 fixed-point integer. Integer division rounds to nearest with ties away from zero, and results saturate to int16. Disabled or zero strength delegates to ordinary generation. Scratch is retained by the worker, and cancellation returns no chunk. This implementation has not been built or tested; seam, cancellation and identity checks remain required.
