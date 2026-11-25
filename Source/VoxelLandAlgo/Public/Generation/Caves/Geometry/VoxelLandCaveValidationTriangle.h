#pragma once

#include "CoreMinimal.h"
#include "Generation/Caves/Geometry/VoxelLandCaveClosedBoxCm.h"

// One indexed CPU-mesh triangle, preserving its original winding and order.
// Bounds enclose the exact sum of the binary mesh-local positions and the
// binary render origin. They include at most one centimetre of outward
// integer rounding per axis; they do not include renderer/transform error.
struct VOXELLANDALGO_API FVoxelLandM5ValidationTriangle
{
	FVector3d A;
	FVector3d B;
	FVector3d C;
	FVoxelLandM5ClosedBoxCm Bounds;
	int32 SourceTriangle = 0;
};
