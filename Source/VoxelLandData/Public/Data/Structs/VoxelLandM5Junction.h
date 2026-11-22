#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5PointCm.h"

// Plan-scoped junction intent. The enclosing plan supplies the signed region.
// CenterCm is a cave-geometry centerline position, not a feet position. Every
// incident route must meet FeetAnchorCm or use a separately validated connector.
struct VOXELLANDDATA_API FVoxelLandM5Junction
{
	bool bHasId = false;
	uint64 Id = 0;
	bool bHasCenter = false;
	FVoxelLandM5PointCm CenterCm;
	bool bHasFeetAnchor = false;
	FVoxelLandM5PointCm FeetAnchorCm;
};
