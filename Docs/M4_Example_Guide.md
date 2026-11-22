# VoxelLand M4: reproducible control-map example

This is an authoring recipe and a set of unrun automation fixtures, not a shipped set of `.uasset` files or evidence that M4 has passed visual, cook, dedicated-server, or cross-platform acceptance. The fixture `VoxelLand.M4.Example.Reproducibility` builds transient source graphs without writing packages. It exercises the control programs, curve bake, finite erosion prepass, overhang path, and repeated chunk generation. It does not create an editor graph or prove that the resulting landscape reads clearly at 2 km.

## Generate the example assets

After building an editor target, launch Unreal Editor with `-VoxelLandCreateM4Examples` and run `VoxelLand.M4.Example.CreateAssets` in Automation. It creates the four project-local packages at the paths below with stable node IDs, three editable control graphs, baked curves, finite erosion, overhang and bounded 100 cm height detail. It refuses to overwrite any of the four package paths. A write failure can leave a partial set: inspect any created files before choosing how to proceed. The reduced erosion counts are for a quick functional example, not a quality or performance claim. Assign a material setup if your project needs one.

In a fresh editor process, pass `-VoxelLandVerifyM4Examples` and run `VoxelLand.M4.Example.VerifyAssets` to check that the saved assets, graph layouts, references, configuration, and baked curves reopen. `VoxelLand.M4.Example.Authoring` exercises transient creation and curve staleness without writing packages. These tests have not been run for this revision.

## Generate a shared height-output example

To generate an editable shared height graph and config at `/Game/VoxelLandExamples/M4/DA_M4_HeightGraph` and `/Game/VoxelLandExamples/M4/DA_M4_HeightWorld`, launch the editor with `-VoxelLandCreateM4HeightExamples` and run `VoxelLand.M4.Example.CreateHeightAssets`. The graph has three independently seeded fBm branches connected to one M4 Height Output; the config adds curves, finite erosion, height detail and overhang. The generator checks both paths and refuses to overwrite existing packages. A write failure can leave a partial pair, so inspect any created files before retrying. The assets are generated only on explicit opt-in; they are not supplied as prebuilt files. A successful save or validation does not itself prove an actual cook.

Run `VoxelLand.M4.Example.HeightAuthoring` to validate the transient example. To check saved assets, launch a fresh editor with `-VoxelLandVerifyM4HeightExamples` and run `VoxelLand.M4.Example.VerifyHeightAssets`. These tests have not been run for this revision.

## Use one graph-authored height output

Create a **VoxelLand M4 Control Graph** asset. A new graph starts with an **M4 Height Output** node containing named Continental, Mountain and Peaks inputs. Connect each input to a 2D control expression; expressions may share upstream nodes. Assign this same graph to all three controls in the M4 world config and edit the three curves on the config. Each input is compiled as its own program with an independent channel seed namespace. The fixed version 1 height stage adds the configured base height and the three curve outputs in continental, mountain, peaks order before erosion; it does not support arbitrary height arithmetic.

To replace the height node, add an **M4 Height Output**, right-click it and choose **Use as Height Output**. A graph using this node must contain exactly one height output and no legacy Density Output nodes. Invalid connections, cycles, 3D controls and filters prevent compilation. Save both graph and config after editing. Existing M3 single-output graphs and earlier M4 graphs with three named Density Outputs continue to work. The earlier three-output graph mode and the new height-output mode cannot be mixed within one graph.

## Create the assets manually

In the Content Browser create three **VoxelLand Terrain Graph** assets and one **VoxelLand M4 World Config**. For a project-local example, suggested paths are `/Game/VoxelLandExamples/M4/DA_M4_Continental`, `/Game/VoxelLandExamples/M4/DA_M4_Mountain`, `/Game/VoxelLandExamples/M4/DA_M4_Peaks`, and `/Game/VoxelLandExamples/M4/DA_M4_World`. These paths are also used by the opt-in generator above; assets are not supplied as prebuilt files. Create new assets at unused paths; do not overwrite the existing M3 example.

Build each control graph with `Position X` and `Position Y` connected to `Fbm2` inputs `X` and `Y`, and connect its `Result` to the `Density Output` input. The output is interpreted as a normalized control value for M4; it is not the final terrain density. Leave the fBm defaults at three octaves, gain `0.5`, and lacunarity `2`. Set the noise frequency in inverse centimeters:

| Graph | Frequency | Example node seed salt |
|---|---:|---:|
| Continental | `0.00002` | `17` |
| Mountain | `0.00006` | `46` |
| Peaks | `0.00012` | `75` |

The runtime also derives separate channel seeds from the world seed. The graph editor supplies stable node IDs; keep the three graph assets and their node IDs if you need an existing world's random streams to remain stable. The exact node GUIDs in manually authored assets need not match the transient fixture, so their output hashes are not expected to match the fixture's hashes. Save each graph successfully so its compiled program is current. M4 control graphs currently reject Z-dependent or 3D operations, density filters, external providers, and unsupported graph dependencies.

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

Save the graphs before saving the M4 config. The editor's config save hook validates the graph programs and bakes the 4,097-entry curves. If the config reports missing, stale, or invalid baked curves, fix and save its inputs again. If cook preparation fails, the config clears baked data, logs the reason and marks the cooked export archive as failed. A full cook still needs to be run to verify integration on the target engine and packaging configuration.

For cook-time asset validation, enable and load Unreal's **Data Validation** plugin and pass `-RunAssetValidation -ValidationErrorsAreFatal` to the cook commandlet. Unreal's content-validation setting must permit validation. With those conditions, M4 config data validation reports invalid settings, missing or stale baked curves, and unsupported or missing compiled-program envelopes as errors; Unreal can reject the affected package when validation errors are fatal. This validation does not prove that compiled graph bytecode matches its source or is valid for M4's 2D control evaluation. The config's cook save hook performs those checks and clears old baked curves if they fail. Its export-archive guard reports a save error when preparation fails or is skipped; end-to-end cook rejection remains unverified until tested with the target cooker. Test the actual packaging configuration and inspect its cook result and logs.

## Preview and runtime

Open a control graph, choose the M4 config in the preview panel, and select **Preview M4 world**. The preview uses the CPU preparation path and a bounded chunk volume. A previous surface remains marked **Stale** while replacement work is pending or invalid. The ordinary preview does not establish the 2 km criterion.

For the bounded long-distance review, select a **finite** M4 config and click **Inspect from 2 km**. This creates a separate inspection descriptor with `800` cm voxels without changing the saved config. It generates LOD 0 chunks `-1..1` in X and Y (a 768 m-wide target) and chooses its vertical chunk range from the completed global height grid, the active overhang band, and clearance. The operation rejects a range above 16 vertical chunks, an unsupported Z range, an empty surface, or mesh buffers exceeding 64 MiB rather than claiming a valid view. Domain padding, filter, and slope footprints must also fit. Unlimited mode has no global height grid and cannot use this inspection profile.

On successful publication, the preview sets a 20° horizontal field of view and places the camera 2 km horizontally from the target and 50 m above it. The target is centered on the central XY chunk and vertically at the midpoint of the global height range. Check the status and **Diagnostics** tab: the global-stage inspector reports the completed grid bounds, resolution, height range, and erosion state; the inspector includes observed per-control ranges, means and endpoint counts, erosion changed-sample and height-difference totals, and a 128 × 128 grayscale map of the final global grid. The image shows +X to the right and +Y upward, scaling the observed range from black to white; a constant field is mid-gray. Three 32 × 32 control thumbnails show continental, mountain, and peaks before erosion from left to right; black means 0 and white means 65535. Both views sample grid points and can miss small features. The height map excludes per-chunk height detail, overhang, and smoothing. It is sampled from the completed grid, so small features can be missed, and is marked stale while a replacement preview is pending. Unlimited mode has no global map.

Click **Copy inspection details** after the view is ready to copy the configuration path, seed, overrides, descriptor settings, chunk extents, RHI, viewport size, and camera transform/FOV. The copy includes the current camera and viewport values, so record it after any view adjustment. It records the full scalar snapshot, graph paths, control hashes and overrides. Retain the referenced assets, graph function dependencies, compiled data and exact source/dependency revisions: hashes and paths do not embed program or LUT bytes and are not a saved world. Manually capture an image and assess whether mountains, ridges, and valleys are visible and readable, recording clipping or missing terrain and repeating for several named seeds. The tool does not capture a screenshot or judge visual acceptance. This is a bounded target viewed from 2 km, not evidence of a 2 km streamed horizon or mixed-resolution LOD.

Finite preview and runtime requests use a shared in-memory prepass coordinator when their proven upstream identities match. Current programs and provider dependencies are validated for each request; a cancelled consumer detaches without cancelling another consumer's matching work. The coordinator is bounded, may reject work when its admission capacity is exhausted, and has no M4 disk cache. Local `bUnlimited` mode evaluates per chunk instead of using this finite global prepass.

For runtime, place an `AVoxelLandWorld` and call `Initialize M4 From Settings` explicitly after the actor has begun play, passing the config and a startup chunk inside the finite domain. `BeginPlay` does not start generation. Initialization can fail or finish asynchronously; inspect its status and error and wait for the startup chunk before relying on the terrain. The complete startup footprint includes stored sample padding and, when enabled, smoothing and slope margins. A dedicated server uses data-only generation. Runtime collision, caves, PCG and replication are not delivered by this M4 example.

To use chunk-local generation instead, enable `bUnlimited` and disable global erosion. This mode does not run a whole-world erosion prepass. The legacy float-control backend remains limited to exact-float XY coordinates within approximately ±2²⁴ cm. Both unlimited backends remain limited to LOD 0, the supported vertical range, and a bounded CPU meshing envelope; this is not an infinite-precision world.

## Opt-in large-coordinate controls

For a different, versioned terrain appearance, set the M4 config to unlimited mode, disable global erosion, and choose **Large-coordinate fixed lattice** as the unlimited backend. Existing worlds default to **Legacy float controls** and retain their previous behavior. This is a compatibility boundary: the two backends do not produce the same terrain, and a fixed-lattice saved world uses identity version 5 without nonzero shaping, or version 6 when fixed-lattice shaping is active. Do not switch an existing saved world expecting the same terrain.

The fixed-lattice backend accepts direct Position X and Position Y inputs to `Value2` or `Fbm2`, scalar constants/float parameters, and scalar add, subtract, multiply, min, max, abs and negate after noise. `Fbm2` uses the new value-noise basis, not the legacy Perlin implementation. Other operators, coordinate transforms, external providers and nonrepresentable settings fail validation with an error. Frequencies are quantized to Q24 cycles/cm and must round into `(0, 1]`; gain is Q24 in `[0,1]`, lacunarity is Q24 in `[1,4]`, with 1–16 octaves and every octave frequency at most 1. Scalar intermediate values must remain within `[-64,64]`.

Fixed-lattice height detail uses independently seeded 2D value noise; fixed-lattice overhang uses independently seeded 3D value noise. Their frequency must be representable in Q24 when enabled with nonzero amplitude. Their noise is rounded to signed Q15 and then passed through the existing bounded Q8 displacement formulas; zero amplitude remains an exact no-op. These stages do not match legacy Perlin appearance.

Version 2 splits signed integer-centimeter coordinates into mathematical floor and Q24 fractions, hashes signed lattice corners with fixed 64-bit unsigned mixing, interpolates them with Q24 quintic fade, and accumulates octaves in order. Q24 conversion, products and interpolation round to nearest with ties away from zero; final controls clamp to `[-1,1]` and quantize to uint16. This avoids the legacy float-position limit but does not make the world mathematically infinite: the CPU mesher and scheduler restrict unlimited M4 chunk coordinates to ±4,194,302 per axis, integral voxel sizes must be in [1, 134217] cm, vertical height bounds remain, and LOD 0 is the only supported level. These are CPU limits, not a guarantee that Unreal renders or streams at those distances: engine world bounds and large-world rendering precision require independent testing. Camera demand converts Unreal rebased coordinates back to global terrain coordinates, and newly published components use the current world origin; origin-shift integration still requires runtime verification on the target engine. Unreal world-origin offsets also have their own finite range. M3 and finite M4 retain the earlier ±32766 chunk limit. Cross-host identity and visual quality have not been measured.

## Verification when authorized

Build the project and plugin, then run `VoxelLand.M4.Example.Reproducibility` in Unreal Automation. The fixture uses a smaller 8 × 8-cell finite domain and the reduced erosion counts above; it compiles and bakes transient inputs, creates an immutable CPU generator, and compares two chunk hashes after requesting them in reverse order. It requires the supported native noise backend. A pass is a focused same-host check, not proof of visual quality, cross-host determinism, packaged-server support, or complete M4 acceptance. Record the commit, engine version, platform, backend and test result. The M1/M2/M3 outstanding evidence remains separate.
