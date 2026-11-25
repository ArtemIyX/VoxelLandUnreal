#pragma once

#include "CoreMinimal.h"

// Scratch input for one route leg. Face indices refer to the CPU mesh freshly
// generated from the supplied chunk, not a cached or rendered mesh.
struct VOXELLANDALGO_API FVoxelLandM5RouteSupportSelection
{
	uint64 RouteId = 0;
	int32 LegIndex = INDEX_NONE;
	TArray<int32> SupportIndices;
};
