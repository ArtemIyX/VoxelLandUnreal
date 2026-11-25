#pragma once

#include "CoreMinimal.h"

// Counts inspected input, not allocated capacity, provider memory, or codec work.
struct VOXELLAND_API FVoxelLandM5IdentityPreflightStats
{
	int64 Elements = 0;
	int64 StringCodeUnits = 0;
	int64 ConfigurationBytes = 0;
	bool bHasOpaqueSoftPaths = false;
};
