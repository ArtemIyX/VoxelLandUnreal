#pragma once

#include "CoreMinimal.h"
#include "Data/Field/Chunks/VoxelLandError.h"
#include "Data/Terrain/Erosion/VoxelLandErosionSettings.h"

namespace VoxelLandM4Erosion
{
// Starts are supplied in deterministic droplet order, in inclusive sample
// coordinates. This kernel intentionally does not define the still-unsettled
// PRNG/seed format. The input and output are row-major Q8 cm samples; OutHeights
// is untouched on error or cancellation. Not yet integrated into world creation.
VOXELLANDALGO_API FVoxelLandError Apply(int32 CellsX, int32 CellsY, TConstArrayView<int32> Heights,
	TConstArrayView<FIntPoint> DropletStarts, const FVoxelLandM4ErosionSettings& Settings, TArray<int32>& OutHeights,
	TFunctionRef<bool()> IsCancelled);
}
