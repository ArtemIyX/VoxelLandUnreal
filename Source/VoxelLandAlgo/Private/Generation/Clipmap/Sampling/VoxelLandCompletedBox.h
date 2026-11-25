#pragma once

#include "../../TerrainOverlay/Filters/VoxelLandTerrainFilterPlan.h"

struct FVoxelLandM7CompletedBox
{
	FInt64Vector First = FInt64Vector(0);
	int32 Axis = 0;
	FVoxelLandM6FilterPlan Plan;
	int64 Work = 0;
};
