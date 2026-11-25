#pragma once

#include "CoreMinimal.h"

// Inclusive, unpadded voxel indices. Every margin is in LOD0 voxels and
// expands each XY side. Callers include any additional source/stencil needs.
struct FVoxelLandM4DomainFootprint
{
	int64 MinX = 0;
	int64 MinY = 0;
	int64 MaxX = 0;
	int64 MaxY = 0;
	int64 VoxelSizeCm = 100;
	int32 PaddingVoxels = 1;
	int32 FilterHaloVoxels = 0;
	int32 SourceMarginVoxels = 0;
};
