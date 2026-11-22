#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Endpoint.h"
#include "Data/Structs/VoxelLandM5RouteFeatureRef.h"
#include "Data/Structs/VoxelLandM5RouteLeg.h"

// Uncertified route intent. Legs are in traversal order; every covered feature
// must be checked against the plan and final mesh before publication. Start and
// End bind the first/last feet positions to graph nodes, including the relevant
// regional portal feet anchor. Each endpoint's explicit role selects the intended
// chamber, entrance-inside/outside, portal-side, or junction feet anchor. Region
// ownership is explicit on each endpoint
// and feature reference; the portal supplies its complete shared key and both
// anchors. Cross-boundary continuity must be validated separately.
struct VOXELLANDDATA_API FVoxelLandM5Route
{
	bool bHasId = false;
	uint64 Id = 0;
	FVoxelLandM5Endpoint Start;
	FVoxelLandM5Endpoint End;
	TArray<FVoxelLandM5RouteLeg> Legs;
	TArray<FVoxelLandM5RouteFeatureRef> CoveredFeatures;
};
