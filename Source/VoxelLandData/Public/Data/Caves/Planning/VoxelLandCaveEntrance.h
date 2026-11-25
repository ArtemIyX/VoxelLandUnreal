#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Geometry/VoxelLandCaveBounds.h"
#include "Data/Caves/Geometry/VoxelLandCavePointCm.h"

// Only this explicitly bounded opening may cross the protected surface roof.
struct FVoxelLandM5Entrance
{
	bool bHasId = false;
	uint64 Id = 0;
	bool bHasConnectionId = false;
	uint64 ConnectionId = 0;
	FVoxelLandM5Bounds OpeningBoundsCm;
	// Intent only: both standing locations and their connection must be proven
	// against the final surface before the entrance can be published.
	bool bHasInsideFeetAnchor = false;
	FVoxelLandM5PointCm InsideFeetAnchorCm;
	bool bHasOutsideFeetAnchor = false;
	FVoxelLandM5PointCm OutsideFeetAnchorCm;
	bool bFlooded = false;
};
