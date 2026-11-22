#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5PointCm.h"
#include "Data/Structs/VoxelLandM5PortalKey.h"

// Transient, shared-boundary geometry; both regions derive and validate the same key.
struct VOXELLANDDATA_API FVoxelLandM5Portal
{
	bool bHasId = false;
	uint64 Id = 0;
	FVoxelLandM5PortalKey Key;
	FVoxelLandM5PointCm CenterCm;
	int32 RadiusCm = 0;
};
