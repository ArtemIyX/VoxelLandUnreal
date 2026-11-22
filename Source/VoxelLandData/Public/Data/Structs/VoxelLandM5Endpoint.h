#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandM5EndpointKind.h"
#include "Data/Structs/VoxelLandM5RegionCoord.h"

// IDs are local to Kind and Region. Portal IDs additionally require full-key
// equality against the referenced portal to detect hash collisions.
struct VOXELLANDDATA_API FVoxelLandM5Endpoint
{
	bool bValid = false;
	EVoxelLandM5EndpointKind Kind = EVoxelLandM5EndpointKind::Chamber;
	FVoxelLandM5RegionCoord Region;
	uint64 Id = 0; // Every bit pattern is a valid ID when bValid is true.
};
