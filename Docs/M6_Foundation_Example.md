# Generation-time foundation across an M4 chunk seam

Create a flat, cave-disabled M4 world and add two hard box stamps in an M6 world configuration. At 100 cm per voxel, the foundation straddles the boundary between chunks `(-1, 0, 0)` and `(0, 0, 0)`.

## Prepare the baseline

Follow the graph compilation and curve baking workflow in the [M4 example](M4_Example_Guide.md). Use a constant float node connected to Density Output for each control graph. Give the Continental, Mountain, and Peaks curves zero output at both endpoints and bake the curves. In the M4 World Config, set:

| Setting | Value |
| --- | --- |
| Voxel size | 100 cm |
| Base height | 76,800 Q8 cm, equal to 300 cm |
| X and Y domain origin | −25,600 cm |
| X and Y control-grid cells | 8 |
| Control-grid spacing | 6,400 cm |
| Horizontal and vertical startup radius | 1 |
| Global erosion, overhang, height detail, compatibility smoothing, slope normalization, caves | Disabled |

For a finite example, leave Unlimited disabled. For the erosion-disabled local mode, enable Unlimited and keep global erosion disabled.

## Author the overlay

In the Content Browser, create a **Data Asset** of class **VoxelLandM6WorldConfig** and assign the existing M4 asset as **Baseline**. Enable the stamp list and add both entries in this order. Leave region filters and re-distance disabled for the numerical reference.

| Entry | Shape and operation | Stable GUID | Centre in global cm | Half extent in cm |
| --- | --- | --- | --- | --- |
| Clearing | Box, Subtract | `4D360005-0000-0000-0000-000000000001` | (0, 600, 360) | (500, 500, 300) |
| Foundation fill | Box, Add | `4D360005-0000-0000-0000-000000000002` | (0, 600, 140) | (400, 400, 140) |

The clear removes ground above the fill. At `(0, 600, 200)` the expected density is **−819**. At `(0, 600, 300)` the expected density is **205**. Densities use 1/1024 of a voxel. The shared padded samples at these positions should agree across both chunks.

To try a region filter afterward, enable filters, add a Smooth stage with a third stable GUID, set the inclusive mask to `(-400, 200, 100)` through `(400, 1000, 350)` cm, and set strength to `32768`. This changes the numerical reference above. Flatten uses an inclusive mask and the same strength range. Make Space clears its whole authored box at hard strength. Optional final re-distance supports a band of one or two voxels and includes its endpoint reads in the total halo.

## Initialize and inspect

After BeginPlay, call **Initialize M6 From Settings** on the Voxel Land World actor with the new M6 asset and startup chunk `(0, 0, 0)`. The return value means the request was accepted. Wait for **On Initialization Finished**. If it fails, inspect **Last Initialization Error** for the rejected setting or startup footprint. For data-only use, enable the actor's **Data Only** property. Request `(-1, 0, 0)` with **Request Data Chunk** in addition to the startup chunk. A dedicated server uses the same CPU field path without render regions.

**Get M6 World Identity** copies the initialized world's versioned bytes when an overlay is active. Identical authored inputs yield the same bytes. The bytes contain the full M4 baseline identity followed by the ordered stamp, filter, and re-distance encodings. The existing M4 saved-world format is for baseline M4 worlds.

The example and the runtime fixture are authored references. Build, automation, cooked-build, dedicated-server, cross-host, and interactive visual results remain pending. See the [M6 task tracker](M6_Task_Tracker.md).
