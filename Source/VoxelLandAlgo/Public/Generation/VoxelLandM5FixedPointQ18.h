#pragma once

#include "CoreMinimal.h"

// Signed fixed-point coordinates in 1/262144 centimetre units.
struct VOXELLANDALGO_API FVoxelLandM5FixedPointQ18
{
	int64 X = 0;
	int64 Y = 0;
	int64 Z = 0;
};
