#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5PointCm.h"

// Transient world-space plan primitive. IDs are plan-scoped, not array positions;
// regional cross-plan identity also requires the enclosing plan's region coordinates.
struct FVoxelLandM5Chamber
{
	bool bHasId = false;
	uint64 Id = 0;
	FVoxelLandM5PointCm CenterCm;
	int32 RadiusCm = 0;
	// The cave-volume center is not a feet position. All incident routes must
	// connect to this candidate anchor or have validated connector routes.
	bool bHasFeetAnchor = false;
	FVoxelLandM5PointCm FeetAnchorCm;
};
