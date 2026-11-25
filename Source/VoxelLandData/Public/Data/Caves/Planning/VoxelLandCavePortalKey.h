#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Planning/VoxelLandCavePortalAxis.h"
#include "Data/Caves/Planning/VoxelLandCaveRegionCoord.h"

// Full canonical key must accompany any compact derived ID so collisions can be detected.
// Encode Salt as its two's-complement uint64 bits. RegionA sorts before RegionB;
// validation must verify adjacency and axis. Grid origin, region size, and vertical
// interval belong to the enclosing descriptor and portal dependency key.
struct FVoxelLandM5PortalKey
{
	int64 WorldSeed = 0;
	FGuid StageId;
	int64 Salt = 0;
	FVoxelLandM5RegionCoord RegionA;
	FVoxelLandM5RegionCoord RegionB;
	EVoxelLandM5PortalAxis Axis = EVoxelLandM5PortalAxis::X;
};
