# Chaos terrain playground

Open `/VoxelLand/Examples/ChaosPlayground/ChaosPlayground` from plugin content and press Play from **Playground Start**. The placed actor explicitly enables Chaos for the session and drops four managed spheres onto the generated terrain. Chaos stays off in regular worlds by default. The **WC_ChaosPlayground** config also loads with Chaos off when reused outside the playground.

To make another playground, place **VoxelLand Chaos Playground** in an empty level and assign a World Config. Avoid placing a separate blocking floor so the props can only land on the voxel terrain.

- Select **Enable Chaos for Playground** on the actor to enable Chaos in this world without changing the assigned config. Alternatively, enable **Chaos Terrain Collision** in a new World Config used by this level.
- Start Play and watch the managed spheres land. Adjust **First Drop Offset** to place the spheres within eight meters above your terrain, where the support probe can find it. Call **Drop Playground Prop** with a world-space position to spawn another sphere, or **Clear Playground Props** to release the playground's holds.
- Use the default Pawn-blocking profile and switch to **Player Collision** or run `viewmode CollisionPawn` to see installed collision geometry. **Visibility Collision** only shows shapes whose profile blocks Visibility.
- The development viewport shows queued fields, mesh work, cooks and installs while collision work is pending. To hide it, run `voxel.Chaos.ShowCookWork 0`. Headless runs use log/tests and Unreal Insights without a viewport.

## Configure a world

Use **World Config → Chaos Collision** to choose query and physics participation, a named project profile or custom Block/Ignore responses, and collision detail. `WorldStatic` is the built-in default. If a project wants a distinct `VoxelLand` object type, add that object channel in its Collision project settings and choose it by name. The plugin leaves project channel slots untouched. Collision objects are static and generate no overlaps.

Fixed collision LOD 0 keeps collision independent of rendering. In mapped mode the six entries correspond to visual LOD 0 through 5. For instance, set entry 0 to collision LOD 1 and entries 1–5 to Disabled. The fallback controls worlds or locations without a published visual LOD. A requested reduction can keep finer geometry where a collapse would invalidate the surface. Inspect that decision through `GetChaosCollisionTileInfo`. Tile overrides govern each logical tile's owned mesh. Canonical seam triangles stay with their owning tile.

For runtime use:

- `SetChaosTerrainCollisionEnabled` toggles the current world without editing the asset. Tile overrides and accepted revisions survive same-world disable and re-enable. A new generation clears them.
- `SetChaosCollisionTileOverride` and `SetChaosCollisionBoundsOverride` select Inherit, Disabled, or Enabled with optional detail. An enabled tile cooks when demanded.
- `RegisterChaosProp` lets a registered simple or convex component wait for support before simulating. The managed safety path requires synchronous physics and Query and Physics. Unregistering a held prop leaves it paused so the game can choose how to release it.
- `UpdateChaosCollisionLease` acquires coverage for a bounded query. Interpret a miss only when the lease reports Ready and the world and target channel are query-enabled. Release a lease when the area is no longer needed.
- `RequestChaosCollisionRebuildForBounds` recooks from current completed fields. `NotifyChaosCollisionSourceChanged` accepts monotonic source revisions following a future terrain update. `ApplyChaosCollisionOverrideRevision` accepts a complete policy batch per revision and discards older revisions. These APIs prepare for future terrain edits and replication. M12 does not supply either transport or brush logic.

## Verify the shipped map

Run these commands with the UE 5.7 Development Editor and your project's full `.uproject` path.

```powershell
$Map = '/VoxelLand/Examples/ChaosPlayground/ChaosPlayground'
& "$Engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" $Project $Map -game -RenderOffscreen -windowed -ResX=1056 -ResY=594 -unattended -nop4 -nosound -stdout -VoxelLandChaosPlaygroundSmoke -VoxelLandChaosPlaygroundCapture
& "$Engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" $Project $Map -server -nullrhi -port=17871 -unattended -nop4 -nosound -stdout -VoxelLandChaosPlaygroundSmoke
```

Both processes report success and exit after every managed sphere settles onto installed voxel collision. The rendered run writes the built-in Player Collision and Lit views under `Saved/Automation/VoxelLandChaosPlayground`. The second command runs the map as a dedicated server without rendering, using the uncooked Development Editor executable.

## Repeatable physics test and trace

Run **VoxelLand.Collision.Chaos.Playground** in the Session Frontend or from the project root with the UE 5.7 Development Editor command.

```powershell
& "$Engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" $Project -unattended -nop4 -nosound -nullrhi -stdout '-ExecCmds=Automation RunTests VoxelLand.Collision.Chaos.Playground' '-TestExit=Automation Test Queue Empty' '-trace=cpu,frame,bookmark,counters' '-tracefile=C:/Temp/VoxelLandChaos.utrace'
```

The test ticks a data-only world with real Chaos and a deterministic plane crossing two chunks. It checks initial holds, trace contact, simulated movement across the seam, a collision rebuild, tile disable and restore, stale revision rejection, and world disable during a real outstanding cook. Open the trace in Unreal Insights and search the bookmarks for **VoxelLand Chaos playground**. For a timed soak, append `-VoxelLandChaosSoakSeconds=1800`. Append `-VoxelLandChaosSlopeCase` to exercise a 5% slope across collision LOD 1 and 2. Use `-VoxelLandChaosBaseline=Disabled` and `-VoxelLandChaosBaseline=Idle` on matched runs to record default-off and enabled-without-demand baselines. Add `-VoxelLandChaosCookDelayMs=250` in Development to delay completed cook delivery and make pending coverage and teardown behavior visible.

## Verification and performance

Windows UE 5.7 Development. The shipped map passed standalone DX12 and dedicated-server NullRHI runs with four props landing. The recorded Player Collision view showed installed triangles only, while Lit showed ordinary terrain and props. A dedicated NullRHI run of the default-off Outdoor Lighting map recorded zero Chaos terrain scopes and zero render uploads.

For burst profiling, replace the test name with `VoxelLand.Collision.Chaos.CookBurst`. The fixture cooks 25 tiles, rebuilds all 25, checks a real Pawn trace on each tile, then releases them. The measured run reached four concurrent cooks and at most two installations or retirements per tick. The numbers below are milliseconds from Unreal Insights on the reference workstation. They describe the observed test load.

| Workload | Measured scope | p95 | p99 | Max |
|---|---|---:|---:|---:|
| Enabled, no consumers | Chaos manager tick | 0.002 | 0.003 | 0.008 |
| Cook and rebuild 25 tiles | Chaos manager tick | 0.281 | 0.337 | 0.337 |
| Cook and rebuild 25 tiles | Activate one body | 0.156 | 0.187 | 0.187 |
| Cook and rebuild 25 tiles | Retire one body | 0.074 | 0.108 | 0.108 |
| Cook and rebuild 25 tiles | Extract collision mesh on worker | 1.214 | 1.262 | 1.262 |
| Cook and rebuild 25 tiles | Chaos worker cook | 0.920 | 0.942 | 0.942 |
| Three moving props, 25 ms delivery delay | Chaos manager tick | 0.050 | 0.072 | 0.263 |
| Three moving props, 25 ms delivery delay | Activate one body | 0.210 | 0.210 | 0.210 |

The burst took 0.069 ms at most for synchronous cook submission, including input capture. The longest whole frame in the burst window was 8.76 ms with NullRHI. The delayed run recorded 36 seam crossings, two rebuild requests, and zero fall-throughs. Estimated byte counters include reserved worker and cook capacity. The 1 ms default is an admission target checked between game-thread operations, so the actual cost of one body install still matters. Profile other targets and machines with their own geometry and physics settings.

### 30-minute delayed cook soak

The flat-seam fixture passed with 25 ms of added completion-delivery delay. Across 3,599.45 simulated seconds, three managed spheres crossed the seam 5,835 times while the fixture requested 359 rebuilds. The run reported zero fall-throughs and zero recoveries, with six holds totaling 1.10 prop-seconds. Peak work was two concurrent cooks, 8 MiB in pending reservations, and 307,584 resident bytes across two bodies. The final lifecycle fixes passed the 27-test regression suite and the separate mixed-detail slope run. The long soak used the earlier validated runtime before those lifecycle fixes.
