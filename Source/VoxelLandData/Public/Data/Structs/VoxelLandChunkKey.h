#pragma once

#include "CoreMinimal.h"

struct FVoxelLandChunkKey
{
	FIntVector Coordinate = FIntVector::ZeroValue;
	uint8 LOD = 0;
};
