# Outdoor terrain lighting

## Play the reference level

Enable **Show Plugin Content** in the Content Browser and open **VoxelLand Content → Examples → OutdoorLighting → OutdoorLighting**. Press Play and fly from **Flight Start**. The map also includes entry, ridge and overlook cameras for repeatable framing.

`WC_OutdoorLighting` uses the Mountain Grass Patches material graph with infinite Height Terrain and geometry AO strength 0.75. The map sets movable sun and sky, virtual shadow maps through the project settings, manual exposure and raster lighting. Its 48 m horizontal window uses the existing clipmap with maximum LOD 1. Set **Terrain Ambient Occlusion Strength** to zero in World Config to disable the geometry bake. Other World Config assets default to zero.

The shared palette master exposes `TerrainDebugMode` 4 for geometry AO and 5 for texture and geometry AO combined. Plain materials keep their original vertex layout. Profile the intended streaming radius and terrain source before enabling AO on another world.

The opt-in `VoxelLand.Lighting.CreateOutdoorDemo` editor automation, run with `-VoxelLandCreateOutdoorDemo`, creates the map and config when both output paths are free. It leaves existing editable assets intact.

## Reference captures

The editor fixture renders two neighboring Surface Nets chunks with the shipped MeadowSlate terrain palette. A four-meter terrace rises at Y = 1,650 cm and crosses the chunk boundary at X = 0. The lower floor stays open to the sky. Both chunks sample the same quantized field, including their halos and geometry ambient occlusion queries.

## Run

Use a Windows UE 5.7 editor build with the project’s DX12, SM6 and virtual shadow map settings. Run from the repository root in PowerShell.

```powershell
$VoxelLandEngine = 'C:/Users/xeuse/Epic Games/UE_5.7'
$VoxelLandProject = (Resolve-Path './VoxelLandExample.uproject').Path
& "$VoxelLandEngine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" $VoxelLandProject -unattended -nop4 -nosplash -nosound -d3d12 -AllowCommandletRendering '-ExecCmds=Automation RunTests VoxelLand.Lighting.OutdoorCapture' '-TestExit=Automation Test Queue Empty'
```

The test also runs from **Session Frontend → Automation → VoxelLand → Lighting → OutdoorCapture**. The reference scene is generated in a transient preview world. Output goes to `Saved/Automation/VoxelLand/OutdoorLighting`. The test reports a skip warning under NullRHI or outside DX12 SM6.

## Captures

The fixture uses a movable directional light and a movable Sky Light with the engine's Sunset Ambient Cubemap. The camera uses manual exposure at +5 EV compensation with physical camera exposure disabled. Bloom, vignette, motion blur and reflections are held off. Every PNG is 256 × 256 pixels.

| Files | Comparison |
|---|---|
| `GeometryAO.png`, `GeometryAONeutral.png` | Overhead visibility mask versus the neutral mask at zero geometry AO strength |
| `CombinedAO.png`, `CombinedAOFar.png` | Texture and geometry AO near the camera, then the far branch where combined AO equals geometry AO |
| `RasterLowSunOverhead.png`, `RasterLowSunOverheadNoShadows.png` | The lower floor in perspective under a 15-degree sun at +2 EV compensation, with sky fill disabled and dynamic shadows toggled |
| `RasterLowSunAO*.png`, `RasterHighSunAO*.png`, `RasterSkyOnlyAO*.png` | AO off/on pairs with Lumen GI disabled. The high sun uses 55-degree elevation and the sky-only pair turns off the sun |
| `LumenScreenTracesOnAO*.png`, `LumenScreenTracesOffAO*.png` | AO off/on at low sun with Lumen GI enabled, using eight captures to refresh the view before reading the last frame |
| `RasterMotion00.png` through `RasterMotion04.png` | Five low-sun positions that move from X = −1,600 cm to X = +1,600 cm at 800 cm intervals, crossing the chunk boundary |

The automation log and `Metrics.txt` record vertex and triangle counts, allocated AO bytes, synchronous fixture bake time, visibility near the bank versus the open floor, brightness in fixed image regions and the maximum geometry mask jump across the seam. Keep the render mode, scalability, camera and material settings matched when comparing runs. Check `LogRHI` in the editor log for the adapter and shader platform.

Use the playable level to profile streaming. Trace `cpu,gpu,counters,frame` in Unreal Insights and inspect `VoxelLand_TerrainAmbientVisibility`, `VoxelLand_BuildM7VisualRevision`, `VoxelLand_RenderResourceSetup`, BasePass and ShadowDepths. The fixture exercises the same-LOD seam. The CPU vertical seam regression compares the real mixed-LOD curtain top with both terrain sources at shared anchors. Lumen's runtime contribution depends on the view since the voxel proxy supplies raster geometry and has no Lumen cards, distance fields or hardware tracing instances.
