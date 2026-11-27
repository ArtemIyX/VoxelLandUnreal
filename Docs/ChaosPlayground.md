# Chaos terrain playground

Open `/VoxelLand/Examples/ChaosPlayground/ChaosPlayground` from plugin content and press Play from **Playground Start**. The placed actor explicitly enables Chaos for the session and drops four managed spheres onto the generated terrain. Chaos stays off in regular worlds by default. The **WC_ChaosPlayground** config also loads with Chaos off when reused outside the playground.

To make another playground, place **VoxelLand Chaos Playground** in an empty level and assign a World Config. Avoid placing a separate blocking floor so the props can only land on the voxel terrain.

- Select **Enable Chaos for Playground** on the actor to enable Chaos in this world without changing the assigned config. Alternatively, enable **Chaos Terrain Collision** in a new World Config used by this level.
- Start Play and watch the managed spheres land. Adjust **First Drop Offset** to place the spheres within eight meters above your terrain, where the support probe can find it. Call **Drop Playground Prop** with a world-space position to spawn another sphere, or **Clear Playground Props** to release the playground's holds.
- Use the default Pawn-blocking profile and switch to **Player Collision** or run `viewmode CollisionPawn` to see installed collision geometry. **Visibility Collision** only shows shapes whose profile blocks Visibility.
- The development viewport shows queued fields, mesh work, cooks and installs while collision work is pending. To hide it, run `voxel.Chaos.ShowCookWork 0`. Headless runs use log/tests and Unreal Insights without a viewport.

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

The test ticks a data-only world with real Chaos and a deterministic plane crossing two chunks. It checks initial holds, trace contact, simulated movement across the seam, a collision rebuild, tile disable and restore, stale revision rejection, and world disable during a real outstanding cook. Open the trace in Unreal Insights and search the bookmarks for **VoxelLand Chaos playground**. For a timed soak, append `-VoxelLandChaosSoakSeconds=1800`. Use `-VoxelLandChaosBaseline=Disabled` and `-VoxelLandChaosBaseline=Idle` on matched runs to record default-off and enabled-without-demand baselines. Add `-VoxelLandChaosCookDelayMs=250` in Development to delay completed cook delivery and make pending coverage and teardown behavior visible.
