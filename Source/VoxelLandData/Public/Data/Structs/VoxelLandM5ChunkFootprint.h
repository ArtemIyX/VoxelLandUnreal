#pragma once

#include "Data/Structs/VoxelLandM5PointCm.h"

// Inclusive logical-centimetre sample bounds, not half-open world ownership.
// Height bounds conservatively enclose the XY slope stencil; their Z range
// matches the density inputs even though height queries are two-dimensional.
struct VOXELLANDDATA_API FVoxelLandM5ChunkFootprint
{
	FVoxelLandM5ChunkFootprint();
	FVoxelLandM5PointCm StoredMin;
	FVoxelLandM5PointCm StoredMax;
	// Baseline samples for the requested carve/roof and owner neighborhoods.
	FVoxelLandM5PointCm BaselineMin;
	FVoxelLandM5PointCm BaselineMax;
	FVoxelLandM5PointCm OwnerMin;
	FVoxelLandM5PointCm OwnerMax;
	FVoxelLandM5PointCm DensityInputMin;
	FVoxelLandM5PointCm DensityInputMax;
	FVoxelLandM5PointCm HeightInputMin;
	FVoxelLandM5PointCm HeightInputMax;
};
