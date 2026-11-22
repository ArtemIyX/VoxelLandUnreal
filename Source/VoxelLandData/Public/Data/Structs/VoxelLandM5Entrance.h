#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Bounds.h"

// Only this explicitly bounded opening may cross the protected surface roof.
struct VOXELLANDDATA_API FVoxelLandM5Entrance
{
	bool bHasId = false;
	uint64 Id = 0;
	bool bHasConnectionId = false;
	uint64 ConnectionId = 0;
	FVoxelLandM5Bounds OpeningBoundsCm;
	bool bFlooded = false;
};
