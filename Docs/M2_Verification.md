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
5. Launch the actual dedicated-server target on a map containing a VoxelLandWorld actor with Start Demo enabled. In a server-side Blueprint, periodically call `Request Data Chunk` for `(0,0,0)` until it returns true (the demo snapshot is prepared asynchronously), then poll `Has Data Chunk` until true, call `Dump Stats`, and finally `Release Data Chunk`. Alternatively initialize from C++ with a worker-created generator and use `RequestChunk`, `FindChunk`, and `ReleaseChunk`. Verify a real dedicated server produces field data without terrain components/proxies or mesh jobs. Exercise startup, shutdown, PIE start/stop, pending jobs, snapshot replacement and teleporting while generation is active.
6. Call `DumpStats` from Blueprint or C++ to log requested, queued, outstanding, ready, regions, geometry counts, CPU field/mesh bytes and cancellation/stale/failure counts. Enable the `counter` and `cpu` trace channels to inspect `VoxelLand/*` counters and generation, meshing, demand, dispatch and publication scopes in Unreal Insights. Counters show the most recently ticked terrain world and are sampled around publication; they are not a multi-world aggregate. `UnhiddenResidentChunks` counts chunks in unhidden regions; actual frustum/occlusion visibility and draw calls require renderer and GPU profiling. Capture Unreal Insights CPU traces and GPU draw/pass statistics on the same route with regions hidden, visible and unloaded. The logged CPU byte counts do not include GPU memory, scratch or deferred render-resource overlap; use the engine profiler for those. Compare frame p50/p95/p99, GT and render-thread cost, draw calls, memory, and upload/recreation latency. Actor grouping alone does not batch primitives.

## Dedicated-server prerequisite for this example project

The current example project has `VoxelLandExample.Target.cs` and `VoxelLandExampleEditor.Target.cs`, but **does not have a Server target**. The selected `C:/Users/xeuse/Epic Games/UE_5.7` engine also has `Engine/Build/InstalledBuild.txt`. Epic's [dedicated-server setup guide for UE 5.7](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-dedicated-servers-in-unreal-engine?application_version=5.7) calls for a source-built engine. Use a UE 5.7 source build with server-target support, or another engine distribution that explicitly includes the required server build and cook support; an installed editor and a NullRHI test do not establish that support. Record the actual engine revision and project association used.

To make the example project buildable as a server, the user must add the following **project-level** file at `Source/VoxelLandExampleServer.Target.cs`. It is not included in the plugin and has not been added or built by this change:

```csharp
using UnrealBuildTool;

public class VoxelLandExampleServerTarget : TargetRules
{
    public VoxelLandExampleServerTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Server;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("VoxelLandExample");
    }
}
```

Associate the project with the selected server-capable UE 5.7 build, regenerate its Visual Studio project files, and build `VoxelLandExampleServer` in **Development Server / Win64**. Keep the existing project and plugin changes intact. FastNoise is enabled by the project and the VoxelLand plugin declares its FastNoise dependency; this target adds the project module rather than introducing a second vendor module entry. Resolve and record any target or module build errors before proceeding.

Create and save the test map and its server-side Blueprint described in step 5 above. Set that map as **Server Default Map** in Maps & Modes, or pass its long package name explicitly when launching. In the editor associated with the server-capable engine, select **Platforms > Windows > Build Target > VoxelLandExampleServer**, set **Binary Configuration > Development**, then use **Platforms > Windows > Content Management > Cook Content**. Ensure the test map and referenced Blueprint are included in the cook; inspect the cook log for success. For a local Windows development build the cooked server content is normally under `Saved/Cooked/WindowsServer` and the executable under `Binaries/Win64/VoxelLandExampleServer.exe`. Launch from the project directory, for example:

```powershell
& '.\Binaries\Win64\VoxelLandExampleServer.exe' '/Game/<Path>/<TestMap>' -log
```

Replace the map path with the actual long package name. Confirm from the server log that the intended map loaded and the request completed; the executable existing or a successful cook alone does not prove the data-only behavior. If a server-capable engine, target, or cooked map is unavailable, record this matrix row as pending rather than substituting an editor run.

## Current coverage and limitations

`VoxelLand.M2` authors padded classification, axis planes, tiny isolated features, index/normal checks, asymmetric seams at several scales including both coordinate envelope boundaries, publication identity decisions, cancellation before meshing, sphere normals and face orientation, a box-subtracted sphere tunnel, X/Y/Z plane crossings with 3-axis shared borders and 2×2×2 ownership, shuffled concurrent generation/meshing parity, component payload/material/visibility contracts, and a latent data-only scheduler scenario using a worker barrier and marked result to force stale completion after same-key reuse. It also replaces the generator with a distinct snapshot while a worker is blocked and checks teardown and the replacement hash; a capacity-saturation scenario checks camera-first admission, retained-chunk eviction, stationary retry and velocity reversal. That scenario also creates and removes a region/component, toggles actor hiding, changes its material and drains queued render commands. Execution and real-RHI render-resource verification remain pending. The simulated data-only scheduler scenario does not substitute for a real dedicated-server test or a real-RHI resource lifecycle test; record those manual checks explicitly. No runtime result should be inferred from a successful compile alone.

The current mesher uses a scale-dependent world-space binary grid and limits chunk coordinates to ±32766 on each axis to avoid losing fractional cell precision in double-precision global calculations; this is a conservative implementation limit, not a measured M1 generator limit. Out-of-range explicit requests fail and streaming demand skips them. Extremely large voxel sizes whose 34-cell local extent is not representable by float vertex buffers are rejected by meshing. Choose an ordinary world scale and record the actual tested precision range before expanding these bounds. Region size is configurable on the world actor and takes effect when initialized; the default 10×10×2 provides 200 metadata slots per region.

Demand uses distance followed by forward-velocity tie breaking. Velocity sectors avoid rebuilding for every speed change, but movement hovering near a sector threshold can still trigger repeated demand rebuilds; capture the `VoxelLand_UpdateDemand` scope while testing that case. `UnhiddenResidentChunks` does not measure actual visible primitives; use the renderer and GPU views in Insights or `stat scenerendering`, `stat unit` and `stat gpu`. Resource creation/release counts, precise upload/deferred-release bytes, and target-hardware budgets are not captured by the plugin counters. Track those through renderer/RHI profiling and report them separately.

Starting performance targets are provisional: compare against design section 21 and report measured values rather than claiming success in advance. Capture material domain compatibility, faces and normal orientation, cave readability, region transitions, memory after queues drain, and exceptions or errors in the output log. The M2 acceptance gate remains pending until the user has run and recorded the full matrix.
