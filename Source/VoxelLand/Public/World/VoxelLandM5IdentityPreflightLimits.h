#pragma once

#include "CoreMinimal.h"

// Caller-selected traversal limits, not an identity, world format, or memory bound.
struct VOXELLAND_API FVoxelLandM5IdentityPreflightLimits
{
	int64 MaxElements = 0;
	int64 MaxStringCodeUnits = 0;
	int64 MaxConfigurationBytes = 0;
};
