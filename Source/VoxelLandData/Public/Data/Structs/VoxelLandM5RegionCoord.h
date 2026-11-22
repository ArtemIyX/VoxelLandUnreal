#pragma once

#include "CoreMinimal.h"

// Signed logical regional-grid indices; their origin is independent of a UWorld origin shift.
struct FVoxelLandM5RegionCoord
{
	int64 X = 0;
	int64 Y = 0;
};
