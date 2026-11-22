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
// Structural coverage checks only. Feature labels do not prove that legs
// geometrically traverse their claimed features, so this function returns
// Unproven after those checks until a mandatory geometry validator exists.
// It never establishes floor, clearance, surface or walkability proof.
// Each route must cover exactly one graph connection whose endpoints match its
// anchors. Each required feature has exactly one coverage owner. More general
// route decompositions return Unproven rather than receiving a false proof.
// The immutable candidate owns its input lease; this function retains it for
// the whole call, uses the same per-plan budget, and does not modify it.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Validate(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& Region, TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
}
