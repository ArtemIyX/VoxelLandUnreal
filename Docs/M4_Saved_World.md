# M4 saved worlds

`FVoxelLandM4SavedWorld` stores a versioned, UObject-free copy of the M4 world snapshot and its density identity. The snapshot includes the three control graph sources and function dependencies, cooked programs, overrides, baked curves, generation settings and startup radii. It does not store a generated prepass grid. Keep a separate copy of the saved record for each world; changing the source assets later does not modify an existing record.

## Capture and persistence

1. On the game thread, call `UVoxelLandM4WorldConfig::CaptureSnapshot` on validated, baked settings. Retain the returned `FVoxelLandM4WorldSnapshot` by value.
2. On a worker thread, call `VoxelLandM4SavedWorld::Capture(Snapshot, Saved, Error)`. It checks the compiled programs, baked curves and density identity. On failure, do not save its output.
3. Persist `FVoxelLandM4SavedWorld` using Unreal property serialization, including its `UPROPERTY` fields. The automation fixture `VoxelLand.M4.SavedWorld.RoundTrip` demonstrates a memory archive wrapped in `FObjectAndNameAsStringProxyArchive`. Store the format version with the entire record, not just the identity bytes.

Save files must come from a **trusted source**. Set a size limit before deserializing an archive. The preflight checks an already-deserialized record and bounds its structural data before job dispatch; it is not a parser hardened against hostile lengths or an exact measurement of engine and allocator memory. Preserve save files across upgrades until a migration/retention policy exists. Restoration requires compatible algorithm, compiler, provider and noise backend versions; cross-version and cross-platform equivalence are not guaranteed.

## Restore and initialize

Deserialize the saved record with the same property serialization approach. On the game thread, after the actor has begun play, call `AVoxelLandWorld::InitializeM4FromSavedWorld(Saved, StartupChunk)`. A `true` return means the bounded asynchronous request was accepted, not that generation succeeded. Wait for `OnInitializationFinished` or inspect `bInitializing` and `LastInitializationError`. The worker verifies the saved identity and current program/provider compatibility before generating; the startup demand must satisfy the saved domain and normal resource budgets. Cancellation and shutdown use the existing actor lifecycle. The actor owns its captured copy, so later changes to the caller's record cannot change an in-flight request.

Material selection is presentation state and is not part of the density identity. Set the actor's material independently; initialization from a saved record does not load the stored material path. Dedicated servers remain data-only. To inspect a record without starting an actor, `VoxelLandM4SavedWorld::Restore` runs on a worker and returns a verified snapshot; it does not reserve a prepass budget or initialize a world. Existing preparation APIs still require their normal resource admission and cancellation handling.

Run `VoxelLand.M4.SavedWorld.RoundTrip`, `VoxelLand.M4.SavedWorld.Admission`, and `VoxelLand.M4.Runtime.Initialization` after building the automation target. The tests in this revision have not been executed as part of implementation.

New world identities use encoding version 3 for the original height stage and version 4 for a graph-authored height stage. Both order valid overrides by parameter GUID and record each validated control graph source fingerprint; version 4 additionally records the graph-height manifest. Existing versions 1, 2, and 3 remain readable for compatible original-stage snapshots; version 1 retains its original order-sensitive comparison; restoring them does not rewrite the record. Unknown identity versions are rejected. If a product chooses to migrate a legacy record, first validate it using `VoxelLandM4WorldIdentity::Matches`, then explicitly capture a new record from the validated snapshot and retain the original until the replacement has been stored successfully. This encoding change does not claim compatibility across algorithm or backend versions.
