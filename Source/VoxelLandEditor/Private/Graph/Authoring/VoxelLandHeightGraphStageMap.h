#pragma once
#include "CoreMinimal.h"

class FVoxelLandM4HeightGrid;

namespace VoxelLandM4StageMap
{
constexpr int32 Resolution = 128;
// Generates top-down BGRA pixels, left to right +X and top to bottom -Y.
// Observed global grid range maps to black/white; a constant field is mid-gray.
// Cancellation or sampling failure leaves OutPixels unchanged.
bool Build(const FVoxelLandM4HeightGrid& Grid, TFunctionRef<bool()> IsCancelled, TArray<uint8>& OutPixels);
}
