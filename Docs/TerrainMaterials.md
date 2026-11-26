# Terrain materials authoring and verification

Mountain Biome uses two editable materials. Meadow covers lower terrain. Slate blends in across higher and steeper faces. The two surfaces tile at different distances, and their source maps are generated from periodic functions. They are ordinary texture assets that you can open and replace.

| Start here | What it shows |
|---|---|
| **Mountain Biome** | Finite Height terrain with the Meadow and Slate palette |
| **Infinite Mountain Biome** | The same authored Height graph and material rules as streaming clipmap terrain |
| **Density Materials** | The same Material Setup applied to Direct Density, including steep faces |
| **Rolling Hills** | The legacy material path for comparison |

Mountain Biome and Rolling Hills are in the VoxelLand Examples folder. The other two configs, the palette, both surface definitions and all six texture sources are in **VoxelLand → Examples → TerrainMaterials**. Select the Mountain Biome World Config in the terrain graph preview to see the full rules. To explore the infinite example in Play, place a **VoxelLand Clipmap Demo** actor and assign **WC_InfiniteMountainBiome** in its instance details.

## Build your own palette

1. Import three tileable textures for each surface. Use square power-of-two sources with wrap addressing. The corresponding maps from every surface must agree in size and mip policy.
2. Create **VoxelLand → Surface Material**. Assign albedo, tangent-space normal and ORMH. Albedo uses sRGB. Normal and ORMH use linear data. ORMH channels are occlusion, roughness, metallic and blend height. An entirely opaque height channel requires explicit confirmation.
3. Set the surface tile period in centimeters. Increase it for broader texture features. Tint changes the sampled color. Height blend contrast changes how strongly the packed height affects a blend.
4. Create **VoxelLand → Material Palette**. Add between one and 32 surfaces. Place the fallback surface in the first slot. Invoke **Bake Palette** and save. The palette records its slot mapping and stores three generated texture arrays. Each bake accepts up to 384 MiB of combined uncompressed top-mip BGRA8 data across the three arrays. This fits 32 surfaces at 1024 × 1024 per map. Reduce resolution or slot count if the bake exceeds the cap.
5. Create **VoxelLand → Material Setup** and select the palette. Add elevation or slope rules. Elevation thresholds and widths use centimeters. Slope uses degrees from up. The blend interval is centered on the threshold. Each later rule layers its surface over the result of preceding rules.
6. Assign that setup to a World Config. Keep the config selected while previewing **Terrain**. Named scalar output previews remain available for inspecting graph data.

After adjusting source maps, tint, period, blend contrast or palette order, rebake and save the palette. For a failed bake, fix the texture or slot named in the diagnostic and retry. The last successful bake remains available during correction. A rule refers to a stable surface ID, so a reordered palette continues to select the intended surfaces after rebaking. Existing setups with a plain Material still use that material.

## Author grass patches with a Material Graph

1. Open **MG_GrassPatches** in **VoxelLand → Examples → TerrainMaterials**. Its preview uses **WC_GrassPatches** and the editable Meadow, Dry Grass and Slate surfaces.
2. Change the noise size to make patches wider or narrower. Adjust the Remap node's **Lower** and **Upper** values to change the grass blend width. The second Remap blends Slate onto steep slopes.
3. For your own world, choose **Create → VoxelLand → Material Graph**, assign it under **World Config → Appearance**, and give the world a Material Setup with a baked palette.
4. Select a Surface node and use **Bind Selected Node** to choose a surface from that palette. Connect surfaces to **Blend Surfaces**, a scalar such as noise to **Alpha**, and the result to **Surface Material**.
5. In the preview target menu, choose **Terrain** for the result, or select a node and preview its output from above. A Surface node shows its weight across the generated terrain.

**MG_MountainGrassPatches** uses a Terrain Signal bound to **Mountain Mask** from the Mountain Biome terrain graph. The binding menu shows named outputs from the selected preview config. Renaming an output preserves its ID. A missing or incompatible output is reported in Diagnostics.

### Supply a runtime biome or moisture map

Create one or more `FVoxelLandWorldSignalGrid` values in logical centimeters. Every grid has a channel name, origin, uniform spacing, dimensions and row-major values. Float values are bilinear. Int and Bool grids use the nearest cell. A World Signal node uses its Default value outside the map. Required channels must exist before the world can use the graph.

Create the read-only map with `FVoxelLandWorldSignalSnapshot::Create` and pass it to `AVoxelLandWorld::SetMaterialWorldSignals` on the game thread. Call it before `InitializeFromConfig` for the initial map, or afterwards to refresh appearance. If the terrain inputs or palette change, reinitialize with the new configuration before refreshing the map. Use the refresh error string or the world's initialization diagnostics to inspect an invalid request.

## Automated checks and examples

Run the tests from the UE 5.7 Automation window or use `UnrealEditor-Cmd.exe` with the project path and the listed arguments.

| Test filter | Arguments and evidence |
|---|---|
| `VoxelLand.Materials.Examples` | Loads the shipped configs and the baked palette, checks its validation state, and checks setup bindings |
| `VoxelLand.Materials.AllSlots` | Bakes and validates a transient 32-surface palette and checks the last slot mapping |
| `VoxelLand.Materials.RenderSeam` | Use `-d3d12 -AllowCommandletRendering` with DX12 SM6. Renders two adjacent packed Surface Nets chunks, reads back near and far base color, and compares phase at positive and negative logical offsets around 10 km |
| `VoxelLand.Materials.WorldModes` | Use the same DX12 arguments. Checks that real terrain actors publish the same palette instance on visible chunks in all four modes, move to a new Infinite Height clipmap revision, and preserve the previous world when appearance rules change during initialization |

The rendered test saves `ChunkSeamNear.png` and `ChunkSeamFar.png` to `Saved/Automation/VoxelLand/TerrainMaterials`. It checks seam coverage, abrupt seam color jumps, and distinct albedo slices on either side of a height blend. Keep it separate from headless automation with `-nullrhi`, which exercises the CPU and asset contracts.

Shared shader or sample content can be generated explicitly in a checkout without the generated packages. Run the generators in order and save their packages with the source changes.

1. `VoxelLand.Materials.CreateMaster` with `-VoxelLandCreateTerrainMaster`
2. `VoxelLand.Materials.CreateExamples` with `-VoxelLandCreateTerrainMaterialExamples`

For an optional 32-slot cook fixture, run `VoxelLand.Materials.AllSlots` with `-VoxelLandCreateTerrainMaterialCookFixture`. It saves `/Game/VoxelLandMaterialVerification/DA_Palette_AllSlots`. Run the Windows cooker with `-Package=/Game/VoxelLandMaterialVerification/DA_Palette_AllSlots+/VoxelLand/Examples/TerrainMaterials/WC_InfiniteMountainBiome+/VoxelLand/Examples/TerrainMaterials/WC_DensityMaterials` to include the fixture, examples, and their shared master. The ordinary unit test builds in memory and leaves no fixture packages on disk.

After a standard Windows cook of the fixture and the example palette, launch a fresh editor command process with `-nullrhi -VoxelLandCookedMaterialRoot="<cook output>/Windows/VoxelLandExample"` and run `VoxelLand.Materials.CookedProbe`. Point it at the directory containing both the cooked `Content` and `Plugins` folders. The test reloads the cooked packages, checks stripped authoring data, formats and mip payloads, and compares their shared master reference. Run `VoxelLand.Materials.RenderSeam` separately to check the shader under DX12.
