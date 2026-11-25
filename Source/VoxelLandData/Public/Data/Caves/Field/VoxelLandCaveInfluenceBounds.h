#pragma once

#include "CoreMinimal.h"

// Transient conservative bounds supplied by the caller, including any radius,
// smoothing, displacement, and halo required by the caller's query contract.
struct FVoxelLandM5InfluenceBounds
{
	uint64 PrimitiveId = 0;
	FBox3d Bounds = FBox3d(ForceInit);
};
