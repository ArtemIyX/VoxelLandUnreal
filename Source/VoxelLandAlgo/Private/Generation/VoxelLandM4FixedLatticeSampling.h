#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandError.h"

class FVoxelLandGenerator;

namespace VoxelLandM4FixedLattice
{
// The control program must already have passed fixed-lattice validation.
FVoxelLandError EvaluateValidatedPoint(const FVoxelLandGenerator& Generator, int64 WorldX, int64 WorldY,
	TArray<int64>& Registers, uint16& OutValue);
}
