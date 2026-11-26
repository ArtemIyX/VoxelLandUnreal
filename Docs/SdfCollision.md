# SDF character collision

VoxelLand can sweep an upright capsule against completed LOD 0 density. Mesh visibility, material AO and render clipmap LOD do not affect the query.

## Play the shipped example

Enable **Show Plugin Content** and open **VoxelLand Content → Examples → Collision → SdfWalking**. Press Play, wait for the pawn to settle on the terrain, then use **WASD** and the mouse. The example uses the outdoor map's infinite Height config.

Run the opt-in `VoxelLand.Collision.CreateWalkDemo` editor automation with `-VoxelLandCreateWalkDemo` to recreate the level when its output path is free.

## Place the walk pawn in another level

1. Open or create a level with a VoxelLand world that initializes in Play. For an infinite Height example, place a **VoxelLand Clipmap Demo** actor and assign a compatible World Config.
2. Place a **VoxelLand SDF Walk Pawn** just above known terrain. Its origin is the capsule center, so leave about one meter of clearance above the surface. Leave extra room around finite Height edges for the terrain field footprint.
3. Assign that actor to **Terrain World** on the walk pawn. Its default **Auto Possess Player** is **Player 0**. If the level has another auto-possessed pawn, disable its auto-possession.
4. Start a single-player Play session. Use **WASD** to walk and the mouse to look. The pawn will settle onto the field after the first collision chunks arrive.

The walk pawn holds its last position when a sweep cannot establish safe motion. **Is Motion Blocked** in the pawn's details indicates this condition. If it remains blocked, check the terrain world's initialization status, pawn placement, and available field around the capsule. Streaming requests are scoped to this pawn and released when it loses possession or ends Play.

The pawn uses a standalone 60 Hz movement driver to demonstrate ground probes, walking and steps against the SDF. Production movement integration needs the game's movement and prediction contract. Recast navigation and rigid-body physics remain separate consumers of terrain data.

## Use the C++ query

1. On the game thread, give each movement consumer a stable `FGuid`. Call `UpdateCollisionDemandForBounds` with the swept capsule's local bounds, expanded for skin and any follow-up probes. Maintain the lease while the consumer moves.
2. Call `CaptureCollisionView` and use the captured origin to convert request endpoints to logical centimeters. The field view owns immutable chunk refs and can be passed to a query worker.
3. Call `FVoxelLandSdfCollision::SweepCapsule` or `OverlapCapsule`. Handle contact, initial penetration, unavailable data and indeterminate work separately. Advance at most to `LastSafeTime` on a blocked sweep.
4. Call `IsCollisionViewCurrent` on the game thread before applying a delayed result. Release the lease on teardown or when changing terrain worlds.

Record `VoxelLand_SdfCapsuleSweep`, `VoxelLand_CaptureCollisionView`, and `VoxelLand_WalkStep` in Unreal Insights. The walk pawn also exposes grounded and blocked counters.
