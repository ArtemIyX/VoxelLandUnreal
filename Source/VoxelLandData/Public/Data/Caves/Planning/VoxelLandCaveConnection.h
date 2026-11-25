#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Planning/VoxelLandCaveEndpoint.h"

struct FVoxelLandM5Connection
{
	bool bHasId = false;
	uint64 Id = 0;
	FVoxelLandM5Endpoint Start;
	FVoxelLandM5Endpoint End;
	bool bLoop = false;
};
