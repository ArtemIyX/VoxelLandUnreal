#pragma once
#include "CoreMinimal.h"
class FVoxelLandGenerator;
class FVoxelLandM4GridLease;
struct FVoxelLandM4WorldSnapshot;

// Shared finite/unlimited M4 preparation for runtime and editor preview.
// Reserve on game thread before capturing an asset snapshot. The caller must
// capture and validate the config and baked curves on the game thread, then
// pass the immutable snapshot to Prepare on a worker. The lease is retained by
// a finite height grid until its final reader is released. Unlimited mode holds
// the reservation only during preparation. Caller owns lifetime, cancellation,
// and a fresh source/config identity check before publishing the result.
namespace VoxelLandM4Preparation
{
VOXELLAND_API TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Reserve();
VOXELLAND_API bool Prepare(const FVoxelLandM4WorldSnapshot& Snapshot,
	const TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>& Lease, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator, FString& OutError);
}
