#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5CaveSettings.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Data/Structs/VoxelLandM5RegionCoord.h"
#include "Generation/VoxelLandM5RouteAnchorChoice.h"
#include "Generation/VoxelLandM5RouteConnectionChoice.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

class FVoxelLandM5Budget;
class FVoxelLandM5CandidateResult;

namespace VoxelLand::M5::RouteBuilder
{
// Constructs scratch route intents from explicit anchors and ordered connection
// segments. Optional explicit feet waypoints prove nominal ideal-tube coverage;
// an empty waypoint list retains the centered inflated-envelope proposal.
// The result is NOT certified, and must not be published as terrain.
// Borrowed choices and candidate input must stay alive through this call. One
// route per connection is supported; unrepresentable choices fail closed.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Build(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& Region, TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TConstArrayView<FVoxelLandM5RouteAnchorChoice> Anchors, TConstArrayView<FVoxelLandM5RouteConnectionChoice> Choices,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>& Out);
}
