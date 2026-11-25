#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Planning/VoxelLandCaveEndpoint.h"
#include "Data/Caves/Geometry/VoxelLandCavePointCm.h"

// Explicit ordered segment chain and endpoint roles for one graph connection.
struct VOXELLANDALGO_API FVoxelLandM5RouteConnectionChoice
{
	bool bHasConnectionId = false;
	uint64 ConnectionId = 0;
	FVoxelLandM5Endpoint Start;
	FVoxelLandM5Endpoint End;
	bool bReverseFirst = false;
	TArray<uint64> SegmentIds;
	// Empty selects the legacy centered proposal. Otherwise there must be one
	// waypoint at each end of the ordered segment chain. These are intent only;
	// the final surface must independently establish support and clearance.
	TArray<FVoxelLandM5PointCm> FeetWaypoints;
};
