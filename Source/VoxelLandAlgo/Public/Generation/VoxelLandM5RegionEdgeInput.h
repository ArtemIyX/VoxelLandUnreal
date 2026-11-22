#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5RegionCoord.h"
#include "Generation/VoxelLandM5PortalOverlap.h"

// Blocks correspond to A and B, respectively. Their borrowed arrays must be
// immutable, retained and charged by their owner throughout the closure check.
struct VOXELLANDALGO_API FVoxelLandM5RegionEdgeInput
{
	FVoxelLandM5RegionCoord A;
	FVoxelLandM5RegionCoord B;
	FVoxelLandM5PortalOverlap SamplesA;
	FVoxelLandM5PortalOverlap SamplesB;
};
