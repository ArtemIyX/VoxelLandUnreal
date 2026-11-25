#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Data/Structs/VoxelLandM5CaveSettings.h"
#include "Data/Structs/VoxelLandM5RegionCoord.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

class FVoxelLandM5Budget;
class FVoxelLandM5CandidateResult;

namespace VoxelLand::M5::RouteTopology
{
// Checks structural references and continuity only. Success does not prove
// that leg geometry traverses the referenced features, nor establish floor,
// clearance, surface, protection, region closure or walkability safety.
// Each route must cover exactly one graph connection whose endpoints match its
// anchors. Each required feature has exactly one coverage owner. More general
// route decompositions return Unproven rather than receiving a false proof.
// The immutable candidate owns its input lease; this function retains it for
// the whole call, uses the same per-plan budget, and does not modify it.
// Portal checks validate logical regional faces and bounded radii. This API
// has no M4 voxel size; M5 world-identity validation remains mandatory to
// establish voxel/grid compatibility before any regional publication.
VOXELLANDALGO_API FVoxelLandM5Diagnostic ValidateStructure(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& Region, TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);

VOXELLANDALGO_API FVoxelLandM5Diagnostic ValidateStructureForJob(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& Region, TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> SourceBudget,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> JobBudget, TFunctionRef<bool()> IsCancelled);

// Runs the structural check and remains fail-closed: success is unavailable
// until all mandatory geometric and final-field validation is integrated.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Validate(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& Region, TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
}
