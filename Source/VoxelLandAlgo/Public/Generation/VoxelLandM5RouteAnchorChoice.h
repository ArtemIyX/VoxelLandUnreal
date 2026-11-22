#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Endpoint.h"
#include "Data/Structs/VoxelLandM5PointCm.h"
#include "Data/Structs/VoxelLandM5PortalKey.h"

// An explicit, uncertified feet anchor. The caller remains responsible for
// demonstrating support and clearance in the final field.
struct VOXELLANDALGO_API FVoxelLandM5RouteAnchorChoice
{
	FVoxelLandM5Endpoint Endpoint;
	FVoxelLandM5PointCm FeetCm;
	// Required for portal choices: the compact endpoint ID alone is not a key.
	bool bHasPortalKey = false;
	FVoxelLandM5PortalKey PortalKey;
};
