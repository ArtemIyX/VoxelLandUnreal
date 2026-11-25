#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5CavePlan.h"
#include "Data/Structs/VoxelLandM5CaveSettings.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

class FVoxelLandM5Budget;
class FVoxelLandM5CandidateResult;

// Generates bounded, deterministic scratch geometry only. This is neither a
// validated cave plan nor permission to publish terrain. Entrances, masks,
// final-field clearance, surface/roof, water and portal handshakes remain
// mandatory downstream checks. Failure or cancellation leaves Out unchanged.
// The shared budget reserves a conservative upper bound of candidate memory
// until the result dies and precharges bounded work even if the build fails.
// Its configured byte/work limits must not exceed the authored per-plan limits.
// IsCancelled must be a bounded, nonallocating, thread-safe native predicate;
// the caller owns and accounts for any external callback resources.
namespace VoxelLand::M5::CandidatePlanner
{
// Explicit version is retained in the immutable plan; unsupported versions fail.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Build(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& Region, int32 GeometryVersion,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>& Out);
VOXELLANDALGO_API FVoxelLandM5Diagnostic Build(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& Region, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>& Out);
}
