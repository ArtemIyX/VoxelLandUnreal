#pragma once

#include "CoreMinimal.h"

struct FVoxelLandM7SampleGrid
{
	FInt64Vector First = FInt64Vector(0);
	FIntVector Size = FIntVector(0);
	int32 Stride = 1;
};
