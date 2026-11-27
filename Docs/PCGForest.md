# VoxelLand PCG forest example

## Preview and edit

1. Enable plugin content in the Content Browser and open **VoxelLand → Examples → PCGForest → ForestPreview**.
2. Select **Forest · Editor Preview** and click **Generate Preview**. Terrain and the forest appear in the level viewport without PIE. Enable **Live Refresh** to regenerate after changes, or use **Refresh Placement** for a manual PCG-only update. **Clear Preview** removes the temporary terrain and trees.
3. Open **MG_Forest** to inspect **Forest Density** in the mask preview. Broad seeded noise creates groves and the slope term reduces density on steep ground. Open **PCG_Forest** to change minimum spacing, the slope limit, or the Forest Density filter threshold. A lower threshold retains more candidates.
4. In either forest actor, select the **Placement** component and edit its layer settings. The supplied cone is a lightweight marker. Replace it with a rooted static mesh or clear **Static Mesh** and assign **Skeletal Mesh**. Keep the asset pivot at the intended ground contact.

Forest Density is bound by its stable output ID. Renaming the Mask Graph output preserves the binding. The Density filter reads the **ForestDensity** PCG attribute written by **Voxel Mask Sample**. Enable **Show Preview Candidate Points** on the Placement component and refresh. Yellow markers show sampled candidates and green markers show accepted placements. The graph ends in points and VoxelLand publishes the selected cosmetic asset under its cell owner.

## Graph compatibility

Use the VoxelLand terrain nodes and supported point-only nodes such as Attribute Filter, Attribute Noise, Density Filter, Merge, Attribute Maths, and Transform Points. VoxelLand validates the graph before execution. Assign static or skeletal meshes in the layer details so instances publish and retire with the correct cell.

## Play and profile

- Start Play from **ForestPreview** for finite terrain and from **ForestInfinite** to stream around the player on infinite terrain. Both use the same mask and PCG graph. Standard PCG point generation and the terrain-aware scheduler run without Chaos terrain collision.
- Tune **Generation Radius**, **Retirement Margin**, and **Visibility Distance** in each layer. Cell, job, and pending field budgets govern how quickly movement fills the requested region. The layer can override the logical height range for terrain that extends beyond the default band around the configured base elevation.
- Use a render-capable UE 5.7 Editor for placement verification. The latent integration tests exercise editor preview, finite runtime, and infinite runtime. In Play the fixture raises the camera at the same XY, moves away, and revisits the center cell to compare placement.

From PowerShell, set the UE 5.7 and project paths and run the command below. Open the captured trace in Unreal Insights and filter CPU timers by **VoxelLand_PCG**.

```powershell
$Trace = Join-Path $env:TEMP 'VoxelLandPCGForest.utrace'
& "$Engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$Project" -RenderOffscreen -unattended -nop4 -nosound -stdout '-ExecCmds=Automation RunTests VoxelLand.PCG.Forest.Integration' '-TestExit=Automation Test Queue Empty' -trace=cpu,frame,bookmark,counters "-tracefile=$Trace"
```

For a longer run, change the test filter to **VoxelLand.PCG.Forest.Integration.InfiniteRuntime** and add `-VoxelLandPCGForestStress`. Its defaults request a 560 m generation radius, cap resident cells at 96, and move the camera about 1.2 km before revisiting the center. Override them with `-VoxelLandPCGForestStressRadiusCm`, `-VoxelLandPCGForestStressCells`, and `-VoxelLandPCGForestStressTravelCm`. Use `-VoxelLandPCGForestStressCellChunks=2` and `-VoxelLandPCGForestStressVerticalCells=0` to profile wider cells on the same height fixture. The test waits three seconds once the destination has four populated cells. Override the hold and concurrency with `-VoxelLandPCGForestStressHoldSeconds` and `-VoxelLandPCGForestStressJobs`. The log records actual XY travel, published cell reach, and requested radius separately.

Pass `-VoxelLandPCGForestSkeletalMesh=/Game/YourAsset.YourAsset` to run the fixture with an imported skeletal mesh. Record that asset, layer settings, view distances, instance counts, and game-thread timings with the result. The provided marker fixture can verify scheduling, deterministic placement, and publication. Long-distance art and performance acceptance needs the project's intended tree assets.

## Windows UE 5.7 reference results

The focused release suite passed **25 tests**. It covered finite editor preview, grayscale Mask Graph preview, native placement in finite and infinite Height and Direct Density, projection, deterministic revisit, and cleanup on source shutdown. Both saved maps opened in standalone. Dedicated NullRHI startup loaded the infinite map, ran terrain work, and recorded zero VoxelLand PCG or render-upload scopes.

The settled skinned fixture used the optional engine mannequin in reference pose, the RTX 5080 and Ryzen 9 9950X3D workstation, 64 m cells, up to 256 resident cells and eight active cell jobs. After moving 1.184 km, the fixture held at the destination for 18 seconds. It recorded 64 populated cells, 431 instances and a farthest published cell center 324 m from the view, with 560 m requested. The fixture returned to the original region and verified the same center placement.

| Unreal Insights scope | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|
| PCG scheduler on game thread | 0.321 | 0.402 | 1.633 |
| Apply a cell on game thread | 0.280 | 0.368 | 0.860 |
| Publish instances on game thread | 0.203 | 0.288 | 0.717 |
| Canonical surface worker | 19.399 | 20.899 | 22.034 |
| Whole game-thread frame during the active test | 9.329 | 14.711 | 28.366 |

All 148 canonical surface jobs ran on background threads. The initial render trace had game-thread canonical sampling up to 6.629 ms. These figures describe the reference workload, including its terrain settings and stock asset. Profile the imported trees and animation setup to establish production distance and frame cost.
