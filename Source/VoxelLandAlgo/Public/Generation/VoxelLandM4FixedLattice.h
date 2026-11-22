#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandError.h"

class FVoxelLandGenerator;

// Unlimited backend version 2. Deterministic Q24 value noise; this is a new
// terrain appearance, not a continuation of legacy float/FastNoise worlds.
namespace VoxelLandM4FixedLattice
{
VOXELLANDALGO_API FVoxelLandError Validate(const FVoxelLandGenerator& Generator);
VOXELLANDALGO_API FVoxelLandError Evaluate(const FVoxelLandGenerator& Generator, int64 OriginX, int64 OriginY,
	int64 SpacingCm, int32 CellsX, int32 CellsY, TArray<uint16>& Out, TFunctionRef<bool()> IsCancelled);
}
