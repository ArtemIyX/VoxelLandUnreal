#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Planning/VoxelLandCaveSettings.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Data/Caves/Planning/VoxelLandCaveRegionCoord.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

class FVoxelLandM5Budget;
class FVoxelLandM5CandidateResult;

// Pure graph validation; success is not a geometry or walkability certificate.
namespace VoxelLand::M5::Topology
{
// Only an immutable candidate produced from these exact inputs and this same
// budget is accepted. Scratch is leased before allocation; work is precharged
// and remains charged on cancellation. The predicate must be bounded,
// nonallocating and thread-safe. Success is not a publication certificate.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Validate(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& Region, TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
VOXELLANDALGO_API FVoxelLandM5Diagnostic ValidateForJob(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& Region, TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> SourceBudget,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> JobBudget, TFunctionRef<bool()> IsCancelled);
}
