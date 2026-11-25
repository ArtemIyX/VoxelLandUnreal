#pragma once

#include "CoreMinimal.h"

// Deterministic proof limits per triangle; exhausting either leaves it unproven.
struct FVoxelLandM5SweepSubdivisionLimits
{
	int32 MaxDepth = 8;
	int32 MaxNodes = 511;
};
