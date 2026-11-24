#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5CaveSettings.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Data/Structs/VoxelLandM5RegionCoord.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

class FVoxelLandM5Budget;
class FVoxelLandM5CandidateResult;

namespace VoxelLand::M5::IdealRouteValidation
{
// Proves route structure and complete, unique traversal of each ideal segment
// by a supported centered or explicit nominal feet path. Explicit paths do not
// prove inflated clearance. This is not a final terrain certificate: floor
// support, quantized mesh, roof/protection, regional closure and player
// walkability must still be validated before publication.
// Candidate provenance and the same shared budget are checked by structure
// validation; callers retain the candidate and all its admission leases.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Validate(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& Region, TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
}
