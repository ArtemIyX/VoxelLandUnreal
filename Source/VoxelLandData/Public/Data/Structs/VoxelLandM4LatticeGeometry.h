#pragma once

#include "CoreTypes.h"

struct FVoxelLandM4LatticeGeometry
{
	static constexpr int32 MaxCellsPerAxis = 1024;
	static constexpr int64 MaxSamples = int64(MaxCellsPerAxis + 1) * (MaxCellsPerAxis + 1);

	int64 OriginX;
	int64 OriginY;
	int64 SpacingCm;
	int32 CellsX;
	int32 CellsY;
};
