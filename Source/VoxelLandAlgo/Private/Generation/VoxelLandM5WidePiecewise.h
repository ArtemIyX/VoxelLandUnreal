#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Generation/VoxelLandM5FixedPointQ18.h"
#include "Generation/VoxelLandM5FixedTriangleQ18.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;

namespace VoxelLand::M5::WidePiecewise
{
[[nodiscard]] FVoxelLandM5Diagnostic Certify(const FVoxelLandM5FixedPointQ18& Start,
	const FVoxelLandM5FixedPointQ18& End, int64 Radius, int64 Height, int64 Skin, int64 Margin,
	int32 SlopeNumerator, int32 SlopeDenominator, TConstArrayView<FVoxelLandM5FixedTriangleQ18> Triangles,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
}
