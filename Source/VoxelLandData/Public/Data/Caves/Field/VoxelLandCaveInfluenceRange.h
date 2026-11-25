#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Geometry/VoxelLandCaveBounds.h"

// LOD0 chunk rectangle and its conservative half-open logical read envelope.
struct VOXELLANDDATA_API FVoxelLandM5InfluenceRange
{
	FVoxelLandM5InfluenceRange();
	FIntVector MinChunk = FIntVector::ZeroValue;
	FIntVector MaxChunkExclusive = FIntVector::ZeroValue;
	FVoxelLandM5Bounds ReadBoundsCm;
	// Post-filter column samples and their raw M4 read dependency. Both are
	// metadata, not evidence that columns have been sampled or certified.
	bool bRequiresPocketColumns = false;
	FVoxelLandM5Bounds PocketColumnBoundsCm;
	FVoxelLandM5Bounds PocketColumnReadBoundsCm;
};
