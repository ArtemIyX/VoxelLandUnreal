#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Generation/VoxelLandM5SupportPlane.h"
#include "Templates/Function.h"

namespace VoxelLand::M5::SupportMargin
{
// Inputs must already have been validated and reduced to the same exact units.
// Success bounds the inflated overlap to the nominal lower-cap footprint.
FVoxelLandM5Diagnostic Check(const FVoxelLandM5PointCm& Start, const FVoxelLandM5PointCm& End, int64 Radius,
	int64 Margin, const FVoxelLandM5SupportPlane& Plane, TFunctionRef<bool()> IsCancelled);
}
