#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5PointCm.h"
#include "Data/Structs/VoxelLandM5SegmentProfile.h"

// Endpoints are logical global centimetres, never chunk-local coordinates.
struct FVoxelLandM5Segment
{
	bool bHasId = false;
	uint64 Id = 0;
	bool bHasConnectionId = false;
	uint64 ConnectionId = 0;
	FVoxelLandM5PointCm StartCm;
	FVoxelLandM5PointCm EndCm;
	int32 RadiusCm = 0;
	FVoxelLandM5SegmentProfile Profile;
};
