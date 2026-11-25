#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Planning/VoxelLandCaveAnchorRole.h"
#include "Data/Caves/Planning/VoxelLandCaveEndpointKind.h"
#include "Data/Caves/Planning/VoxelLandCaveRegionCoord.h"

// IDs are local to Kind and Region. Route endpoints require an explicit role
// compatible with Kind; connection topology alone may leave it None. Portal IDs
// additionally require full-key equality to detect hash collisions, and Region
// must match the region selected by its A/B feet-anchor role.
struct FVoxelLandM5Endpoint
{
	bool bValid = false;
	EVoxelLandM5EndpointKind Kind = EVoxelLandM5EndpointKind::Chamber;
	EVoxelLandM5AnchorRole AnchorRole = EVoxelLandM5AnchorRole::None;
	FVoxelLandM5RegionCoord Region;
	uint64 Id = 0; // Every bit pattern is a valid ID when bValid is true.
};
