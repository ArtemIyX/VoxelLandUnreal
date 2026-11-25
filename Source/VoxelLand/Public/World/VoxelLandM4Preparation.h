#pragma once
#include "CoreMinimal.h"
class FVoxelLandGenerator;
class FVoxelLandM4GridLease;
struct FVoxelLandM4WorldSnapshot;
struct FVoxelLandM6WorldOverlay;

// Shared finite/unlimited M4 preparation for runtime and editor preview.
// Finite callers may use EnsureCoordinatorReady on the game thread, capture
// validated bounded inputs, then pass a null lease to Prepare on a worker for
// key-first shared prepass admission. An explicit lease retains the legacy
// unshared path and is held by its grid until the final reader releases it.
// Unlimited mode requires Reserve on the game thread before capture and holds
// the reservation only during preparation. Callers own their lifetime,
// cancellation, and fresh source/config identity checks before publication.
namespace VoxelLandM4Preparation
{
// Called on the game thread before finite requests capture a snapshot. Pins
// module code and enables key-first admission without reserving a grid yet.
VOXELLAND_API bool EnsureCoordinatorReady();
// Bounded per-consumer allowance for snapshot capture and validation before
// key-first producer admission. Hold it until the captured snapshot is freed.
VOXELLAND_API TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> ReserveValidation();
// Module lifecycle only. Shutdown cancels and drains all shared producers.
VOXELLAND_API void StartCoordinator();
VOXELLAND_API void ShutdownCoordinator();
VOXELLAND_API TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Reserve();
VOXELLAND_API bool Prepare(const FVoxelLandM4WorldSnapshot& Snapshot,
	const TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>& Lease, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator, FString& OutError);
// Shared finite callers must retain a validation permit obtained before capture.
VOXELLAND_API bool Prepare(const FVoxelLandM4WorldSnapshot& Snapshot,
	const TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>& Lease,
	const TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>& ValidationLease,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator,
	FString& OutError);
// The optional immutable overlay applies only to the final terrain generator.
VOXELLAND_API bool Prepare(const FVoxelLandM4WorldSnapshot& Snapshot,
	const TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>& Lease,
	const TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>& ValidationLease,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator,
	FString& OutError, const FVoxelLandM6WorldOverlay* Overlay);
}
