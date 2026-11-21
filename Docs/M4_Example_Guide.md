# VoxelLand M4: reproducible control-map example

This is an authoring recipe and an unrun automation fixture, not a shipped set of `.uasset` files or evidence that M4 has passed visual, cook, dedicated-server, or cross-platform acceptance. The fixture `VoxelLand.M4.Example.Reproducibility` builds transient source graphs without writing packages. It exercises the control programs, curve bake, finite erosion prepass, overhang path, and repeated chunk generation. It does not create an editor graph or prove that the resulting landscape reads clearly at 2 km.

## Create the assets

In the Content Browser create three **VoxelLand Terrain Graph** assets and one **VoxelLand M4 World Config**. For a project-local example, suggested paths are `/Game/VoxelLandExamples/M4/DA_M4_Continental`, `/Game/VoxelLandExamples/M4/DA_M4_Mountain`, `/Game/VoxelLandExamples/M4/DA_M4_Peaks`, and `/Game/VoxelLandExamples/M4/DA_M4_World`. These are suggested paths, not files supplied by this plugin. Create new assets at unused paths; do not overwrite the existing M3 example.

Build each control graph with `Position X` and `Position Y` connected to `Fbm2` inputs `X` and `Y`, and connect its `Result` to the `Density Output` input. The output is interpreted as a normalized control value for M4; it is not the final terrain density. Leave the fBm defaults at three octaves, gain `0.5`, and lacunarity `2`. Set the noise frequency in inverse centimeters:

| Graph | Frequency | Example node seed salt |
|---|---:|---:|
| Continental | `0.00002` | `17` |
| Mountain | `0.00006` | `46` |
| Peaks | `0.00012` | `75` |

The runtime also derives separate channel seeds from the world seed. The graph editor supplies stable node IDs; keep the three graph assets and their node IDs if you need an existing world's random streams to remain stable. The exact node GUIDs in manually authored assets need not match the transient fixture, so their output hashes are not expected to match the fixture's hashes. Save each graph successfully so its compiled program is current. M4 control graphs currently reject Z-dependent or 3D operations, density filters, external providers, and graph functions.

Assign each graph to its matching control in the M4 config. Enter three strictly increasing curve keys in each control. Inputs are normalized integers in `[0, 65535]`; outputs are **Q8 centimeters**, meaning `256` units per centimeter. The current height expression adds the three curve outputs to `Base Height Q8` in continental, mountain, then peaks order.

| Control | Input | Output Q8 | Height contribution |
|---|---:|---:|---:|
| Continental | `0` | `-3072000` | -120 m |
| Continental | `32768` | `0` | 0 m |
| Continental | `65535` | `6144000` | 240 m |
| Mountain | `0` | `0` | 0 m |
| Mountain | `32768` | `512000` | 20 m |
| Mountain | `65535` | `3584000` | 140 m |
| Peaks | `0` | `-768000` | -30 m |
| Peaks | `32768` | `0` | 0 m |
| Peaks | `65535` | `1536000` | 60 m |

Set seed `271828`, voxel size `100` cm, base height `0`, and leave the finite-domain defaults for a 16.384 km square. Enable global erosion. The initial defaults of 4,096 droplets, 64 steps, and eight thermal iterations are unmeasured starting values; smaller settings such as 16 droplets, eight steps, and one thermal iteration make a quicker functional example, not a quality or performance claim. Enable the overhang option with amplitude `16384` Q8 (64 cm), band half-width `256000` Q8 (1,000 cm), and frequency `0.001` per cm. Optional slope normalization and the final Smooth Density setting are separate from the control graphs.

Save the graphs before saving the M4 config. The editor's config save hook validates the graph programs and bakes the 4,097-entry curves. If the config reports missing, stale, or invalid baked curves, fix and save its inputs again. The current invalid-cook path clears baked data and logs an error; whether that log aborts an actual cook remains to be verified.

## Preview and runtime

Open a control graph, choose the M4 config in the preview panel, and select **Preview M4 world**. The preview uses the CPU preparation path and a bounded chunk volume. A previous surface remains marked **Stale** while replacement work is pending or invalid. This preview is for inspection; it is not the recorded 800 cm-voxel, 3 × 3 area and 2 km camera review required by the M4 plan.

For runtime, place an `AVoxelLandWorld` and call `Initialize M4 From Settings` explicitly after the actor has begun play, passing the config and a startup chunk inside the finite domain. `BeginPlay` does not start generation. Initialization can fail or finish asynchronously; inspect its status and error and wait for the startup chunk before relying on the terrain. The complete startup footprint includes stored sample padding and, when enabled, smoothing and slope margins. A dedicated server uses data-only generation. Runtime collision, caves, PCG and replication are not delivered by this M4 example.

To use chunk-local generation instead, enable `bUnlimited` and disable global erosion. This mode does not run a whole-world erosion prepass. Its current implementation remains limited to LOD 0, the supported vertical range, and exact-float XY coordinates within approximately ±2²⁴ cm; it is not an infinite-precision world.

## Verification when authorized

Build the project and plugin, then run `VoxelLand.M4.Example.Reproducibility` in Unreal Automation. The fixture uses a smaller 8 × 8-cell finite domain and the reduced erosion counts above; it compiles and bakes transient inputs, creates an immutable CPU generator, and compares two chunk hashes after requesting them in reverse order. It requires the supported native noise backend. A pass is a focused same-host check, not proof of visual quality, cross-host determinism, packaged-server support, or complete M4 acceptance. Record the commit, engine version, platform, backend and test result. The M1/M2/M3 outstanding evidence remains separate.
