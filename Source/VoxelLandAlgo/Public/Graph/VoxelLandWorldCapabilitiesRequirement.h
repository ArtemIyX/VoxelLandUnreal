#pragma once

#include "CoreMinimal.h"

// The bounded scope of data an operation needs to produce its result.
// FiniteGlobal requires a complete finite world and is unavailable in an
// infinite world. BoundedRegion requires only a bounded neighboring footprint.
enum class EVoxelLandWorldRequirement : uint8
{
	Unspecified,
	SampleLocal,
	BoundedNeighborhood,
	BoundedRegion,
	FiniteGlobal
};
