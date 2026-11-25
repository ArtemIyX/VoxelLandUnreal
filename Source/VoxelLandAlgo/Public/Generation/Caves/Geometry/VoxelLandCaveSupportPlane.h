#pragma once

#include "Data/Caves/Geometry/VoxelLandCavePointCm.h"

// Exact plane through Origin: (Z-Origin.Z)*Denominator =
// GradientX*(X-Origin.X) + GradientY*(Y-Origin.Y).
// All values are integer centimetres or dimensionless integer coefficients.
struct VOXELLANDALGO_API FVoxelLandM5SupportPlane
{
	FVoxelLandM5PointCm Origin;
	int32 GradientX = 0;
	int32 GradientY = 0;
	int32 Denominator = 1;
};
