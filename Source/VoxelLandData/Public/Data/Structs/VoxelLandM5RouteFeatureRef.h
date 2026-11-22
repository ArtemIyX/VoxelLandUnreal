#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandM5RouteFeatureKind.h"
#include "Data/Structs/VoxelLandM5RegionCoord.h"

// Kind, signed region and plan-scoped ID together identify a feature. A portal
// reference must additionally resolve and compare the full shared portal key.
struct FVoxelLandM5RouteFeatureRef
{
	bool bValid = false;
	EVoxelLandM5RouteFeatureKind Kind = EVoxelLandM5RouteFeatureKind::Chamber;
	FVoxelLandM5RegionCoord Region;
	uint64 Id = 0; // Zero is a valid ID when bValid is true.
};
