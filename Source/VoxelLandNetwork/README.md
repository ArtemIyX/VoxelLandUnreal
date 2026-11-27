# VoxelLandNetwork

`VoxelLandNetwork` is the runtime module for multiplayer terrain startup and admission. **Current status:** the module contains its loading hooks. The M15 behavior below is planned. You can configure and verify Iris in a host project today, but enabling it alone does not enable VoxelLand terrain replication.

## Planned multiplayer flow

1. The server resolves the selected recipe and prepares the authoritative terrain. A separate VoxelLand session actor replicates the seed, recipe ID, world generation identity, and expected fingerprints. The recipe resolves against cooked project content. The fingerprint covers terrain-affecting dependencies. Mod loading and pack manifests belong to a future milestone.
2. A joining client receives the session actor and an owner-only spawn ticket. The client resolves matching local content and regenerates the terrain for its spawn area. Terrain fields, meshes, and cosmetic PCG instances are generated locally.
3. The client sends readiness through a VoxelLand endpoint actor that the server automatically creates and assigns to the owning PlayerController. The server checks the current world and ticket, verifies generation evidence, and independently prepares the required LOD 0 fields and halo before allowing possession.
4. Late joiners receive the current descriptor. Map travel retires the old session actor, player endpoints, and pending work. The destination gameplay world publishes a fresh descriptor and issues fresh tickets, including when a PlayerController survives seamless travel.

The host chooses its own `GameStateClass`, recipe, and spawn rules. VoxelLand sets up networking automatically for active VoxelLand gameplay sessions. Host projects add no GameState or PlayerController subclass, component, or networking actor. The existing spawn flow calls the admission API before possession. Failures expose a stable numeric enum, a gameplay tag, a developer message, and an optional diagnostic fingerprint. The host decides how to recover. Listen hosts use their authoritative terrain locally. Dedicated servers prepare data without voxel rendering or cosmetic PCG work. Single-player uses the same preparation and spawn-readiness path without Iris or a gameplay NetDriver.

> VoxelLand multiplayer replication requires Iris on the active gameplay NetDriver. The planned runtime check will log a warning and block VoxelLand multiplayer admission if the driver uses another backend. The host can surface that failure to players.

## Enable Iris in an Unreal Engine 5.7 project

### 1. Enable the Iris plugin

Close running sessions and enable **Iris** under **Edit → Plugins**. Alternatively, add this entry to the existing `Plugins` array in your project's `.uproject` file.

```json
{
  "Name": "Iris",
  "Enabled": true
}
```

Use the same project settings for the game client, listen host, and dedicated server. Check that the plugin is enabled for the packaged targets you intend to use.

### 2. Enable Iris at runtime

Merge these settings into your project's `Config/DefaultEngine.ini`.

```ini
[SystemSettings]
net.Iris.UseIrisReplication=1
net.SubObjects.DefaultUseSubObjectReplicationList=1
```

The first setting selects Iris for eligible drivers. The second enables the registered subobject list. If your game currently overrides `ReplicateSubobjects` to replicate custom subobjects, migrate those subobjects to the registered list as part of adopting Iris. If your game uses Replication Graph, migrate the gameplay driver away from it since Iris and Replication Graph are mutually exclusive on a driver. [Epic UE 5.7 migration guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/migrate-to-iris-in-unreal-engine?application_version=5.7)

### 3. Check the gameplay driver

UE 5.7's stock `Engine/Config/BaseEngine.ini` already opts the default `GameNetDriver` definition into Iris. If your project replaces that setting or uses another driver definition, check the merged `IrisNetDriverConfigs` and make sure the gameplay driver supports Iris. The entry below is the stock definition. Add a corresponding project entry only if needed, using the definition name your game actually uses. Check for a driver-name override, which takes priority over a definition match.

```ini
[/Script/Engine.Engine]
+IrisNetDriverConfigs=(NetDriverDefinition=GameNetDriver,bCanUseIris=true)
```

### 4. Check C++ build rules

For each C++ module that directly uses Iris APIs, add `SetupIrisSupport(Target);` inside its `.Build.cs` constructor. Modules using only ordinary replicated properties and RPC declarations can keep their existing build rules.

The UE 5.7 installation used by this project already builds with Iris support and its `TargetRules.cs` has no `bUseIris` property, although Epic's setup guide lists one. Add `bUseIris = true;` only if the engine version or fork you build against exposes that rule. Apply any required build-rule changes to the targets you ship, including the dedicated server. [Epic UE 5.7 Iris setup guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/introduction-to-iris-in-unreal-engine?application_version=5.7)

### 5. Verify the active backend

Restart the editor or launch fresh packaged processes after changing project settings. In each multiplayer process, inspect the gameplay world's NetDriver once it finishes initialization. In C++, a non-null result from `NetDriver->GetReplicationSystem()` confirms Iris on that driver. A console variable value by itself only shows the requested setting.

- **Listen server:** connect a remote client and confirm Iris on the host and remote client.
- **Dedicated server:** launch the server target with a separate client and confirm Iris in both processes. Repeat the check in the destination gameplay world after travel.
- **Startup overrides:** `-UseIrisReplication=1` requests Iris for eligible drivers. Check client and server launch arguments for `-UseIrisReplication=0`, which disables it.
- **Once M15 is implemented:** verify late join, spawn gating, and travel. Use `-UseIrisReplication=0` in a separate negative test to check that VoxelLand reports the unsupported backend and blocks terrain admission. Verify single-player startup without a NetDriver.
