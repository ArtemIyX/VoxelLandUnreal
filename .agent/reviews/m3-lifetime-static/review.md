# M3 static lifetime review

Scope: working-tree M3 changes in VoxelLand and direct context. Build and tests were not run because the user explicitly prohibited builds and the installed editor binaries predate these changes. Findings are static and unverified at runtime.

## High: Demo initialization can outlive runtime module

- File: Source/VoxelLand/Private/VoxelLand.cpp:19
- Evidence: shutdown drains actors only when PendingInitialization or State exists. StartDemo in Source/VoxelLand/Private/World/VoxelLandWorld.cpp queues an untracked thread-pool lambda and later a game-thread callback before either exists.
- Impact: module shutdown/hot reload during a pending demo can leave work and callback code executing after its module unloads.
- Fix: track/drain demo preparation in the same managed job service and prevent queued callbacks after module shutdown.
- Confidence: high.

## High: Editor close failure leaves raw delegates and toolkit alive during unload

- File: Source/VoxelLandEditor/Private/VoxelLandEditor.cpp:43
- Evidence: StopEditors calls Stop then CloseWindow; Stop only cancels/drains jobs and removes ticker. Raw details, graph and PIE callbacks remain until toolkit destruction in VoxelLandGraphToolkit.cpp:272-280, 334-343. Engine FAssetEditorToolkit::CloseWindow closes only when OnRequestClose permits it, and this call does not establish destruction.
- Impact: a retained toolkit/denied close during hot reload can retain UI callbacks or vtables into unloaded module code.
- Fix: remove all external registrations in Stop and guarantee editor instances are closed/destroyed, or make module unload unavailable while one survives.
- Confidence: high.

## Follow-up review

The demo initialization change now tracks and waits for its future, and publishes by game-thread polling. The editor change removes raw delegates in Stop and disables dynamic module reload, mitigating the reported unload risk. UE 5.7 CloseWindow nevertheless always returns true even when OnRequestClose refuses, so the new warning branch cannot detect a retained editor during process shutdown; that lifecycle is unverified.

Medium: `FVoxelLandGenerator::CreateWorker` resets an existing output worker before checking the game thread. If called on the game thread with an existing worker, the context destructor's thread-affinity assertion fires before GameThreadNotAllowed can be returned. Restore the thread check before resetting and document output ownership. Confidence: high.

Optional custom SIMD runs scalar first and compares its result with an independent SIMD worker, so scalar remains authoritative and tails are preserved; this doubles execution for enabled batches. No build or tests were run.
