#pragma once

#include "CoreMinimal.h"
#include "Data/Field/Chunks/VoxelLandError.h"

class FVoxelLandGenerator;

namespace VoxelLandM4FixedLattice
{
// The program must already have passed fixed-lattice validation.
FVoxelLandError EvaluateValidatedPoint(
	const FVoxelLandGenerator& Generator, int64 WorldX, int64 WorldY, TArray<int64>& Registers, uint16& OutValue);
// Height Scale must be in the admitted [1, 2000000] cm range.
FVoxelLandError EvaluateValidatedHeightPoint(const FVoxelLandGenerator& Generator, int64 X, int64 Y, int32 BaseQ8,
	int32 ScaleCm, TArray<int64>& Registers, int32& OutHeight);
}
