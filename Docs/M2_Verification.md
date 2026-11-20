# VoxelLand M2 manual verification handoff

Status: implementation under review; no M2 build, automation, dedicated-server run, RHI validation or performance measurement has been performed by the agent. Existing M1 cross-host and SIMD compile-out results remain independent pending items. Record the plugin and FastNoise commits, UE version, operating system, target configuration, GPU/RHI and selected seed with each result.

## Setup and execution

1. Build the UE 5.7 project and plugin using the `VOXEL-2` plugin checkout. Verify a dedicated-server target separately, rather than treating an editor NullRHI run as equivalent.
2. Run automation in an editor commandlet using the equivalent of:

   ```powershell
   & '<UE>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<project>/VoxelLandExample.uproject' -unattended -NullRHI -nosplash '-ExecCmds=Automation RunTests VoxelLand.M2;Quit' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=<reports>/m2'
   ```

3. In a test map place a `VoxelLandWorld` actor. Enable `Start Demo`, choose seed and material, and use a `DefaultPawn`/spectator with flying movement. Set camera above the surface; cave pockets use 3D noise. The demo does not provide collision. For material seams use a surface material with world-space projection; UV0 is local planar mapping.
4. Fly across positive/negative X, Y, Z chunk and region boundaries, then below the surface and through visible pockets. At 6 m/s fly the same route for at least ten minutes; repeat at 30 m/s as stress. Check seams, front-facing lighting, shadows, hidden or stale chunks and frame hitches. Test world material replacement and region hide/unhide through the provided C++ APIs.
5. From C++, initialize a server-side world actor with a worker-created generator, explicitly call `RequestChunk`, inspect `FindChunk` after completion, and `ReleaseChunk`. Verify a real dedicated server produces field data without terrain components/proxies or mesh jobs. Exercise startup, shutdown, PIE start/stop, pending jobs, snapshot replacement and teleporting while generation is active.
6. Call `DumpStats` from Blueprint or C++ to log requested, queued, outstanding, ready, regions, geometry counts, CPU field/mesh bytes and cancellation/stale/failure counts. Capture Unreal Insights CPU traces and GPU draw/pass statistics on the same route with regions hidden, visible and unloaded. The logged CPU byte counts do not include GPU memory, scratch or deferred render-resource overlap; use the engine profiler for those. Compare frame p50/p95/p99, GT and render-thread cost, draw calls, memory, and upload/recreation latency. Actor grouping alone does not batch primitives.

## Current coverage and limitations

`VoxelLand.M2` authors padded classification, axis planes, tiny isolated features, index/normal checks, asymmetric negative-coordinate seams at several scales, publication identity decisions, cancellation before meshing, and component payload/material/visibility contracts. Execution and actual render-thread resource release remain pending. The synthetic identity and component tests do not substitute for an end-to-end asynchronous cancellation, lifecycle, or real-server test; record those manual checks explicitly. No runtime result should be inferred from a successful compile alone.

The current mesher uses a scale-dependent world-space binary grid and limits chunk coordinates to ±32766 on each axis to avoid losing fractional cell precision in double-precision global calculations; this is a conservative implementation limit, not a measured M1 generator limit. Out-of-range explicit requests fail and streaming demand skips them. Extremely large voxel sizes whose 34-cell local extent is not representable by float vertex buffers are rejected by meshing. Choose an ordinary world scale and record the actual tested precision range before expanding these bounds.

Starting performance targets are provisional: compare against design section 21 and report measured values rather than claiming success in advance. Capture material domain compatibility, faces and normal orientation, cave readability, region transitions, memory after queues drain, and exceptions or errors in the output log. The M2 acceptance gate remains pending until the user has run and recorded the full matrix.
