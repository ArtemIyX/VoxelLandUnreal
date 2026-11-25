#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Planning/VoxelLandCaveRegionCoord.h"
#include "Generation/Caves/Planning/VoxelLandCavePortalOverlap.h"

// Blocks correspond to A and B, respectively. Their borrowed arrays must be
// immutable, retained and charged by their owner throughout the closure check.
struct VOXELLANDALGO_API FVoxelLandM5RegionEdgeInput
{
	FVoxelLandM5RegionCoord A;
	FVoxelLandM5RegionCoord B;
	FVoxelLandM5PortalOverlap SamplesA;
	FVoxelLandM5PortalOverlap SamplesB;
};
