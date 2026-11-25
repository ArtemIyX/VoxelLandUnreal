#pragma once

#include "Data/Caves/Geometry/VoxelLandCavePointCm.h"

// Exact logical centimetre coordinates, counter-clockwise as viewed from above.
// Callers must establish exact correspondence with the actual final mesh.
struct VOXELLANDALGO_API FVoxelLandM5FloorTriangleCm
{
	FVoxelLandM5PointCm A;
	FVoxelLandM5PointCm B;
	FVoxelLandM5PointCm C;
};
