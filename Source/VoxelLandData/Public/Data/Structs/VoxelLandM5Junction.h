#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5PointCm.h"

// Plan-scoped junction intent. The enclosing plan supplies the signed region;
// every incident route must meet this feet anchor or use a validated connector.
struct VOXELLANDDATA_API FVoxelLandM5Junction
{
	bool bHasId = false;
	uint64 Id = 0;
	bool bHasFeetAnchor = false;
	FVoxelLandM5PointCm FeetAnchorCm;
};
