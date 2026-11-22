#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Endpoint.h"

// Explicit ordered segment chain and endpoint roles for one graph connection.
struct VOXELLANDALGO_API FVoxelLandM5RouteConnectionChoice
{
	bool bHasConnectionId = false;
	uint64 ConnectionId = 0;
	FVoxelLandM5Endpoint Start;
	FVoxelLandM5Endpoint End;
	bool bReverseFirst = false;
	TArray<uint64> SegmentIds;
};
