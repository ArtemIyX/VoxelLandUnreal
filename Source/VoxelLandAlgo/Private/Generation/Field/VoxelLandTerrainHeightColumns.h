#pragma once

#include "Containers/Array.h"
#include "CoreTypes.h"

struct FVoxelLandTerrainHeightColumns
{
	TArray<int32> Centers;
	TArray<int32> PlusX;
	TArray<int32> MinusX;
	TArray<int32> PlusY;
	TArray<int32> MinusY;
};
