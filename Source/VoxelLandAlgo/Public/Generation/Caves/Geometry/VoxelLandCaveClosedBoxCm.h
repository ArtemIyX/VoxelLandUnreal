#pragma once

#include "Data/Caves/Geometry/VoxelLandCavePointCm.h"

// Inclusive integer-centimetre bounds. A mesh-derived box must be rounded
// outward by its producer, including all conversion and predicate error.
struct VOXELLANDALGO_API FVoxelLandM5ClosedBoxCm
{
	FVoxelLandM5PointCm Min;
	FVoxelLandM5PointCm Max;
};
