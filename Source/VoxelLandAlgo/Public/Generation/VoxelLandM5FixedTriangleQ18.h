#pragma once

#include "CoreMinimal.h"
#include "Generation/VoxelLandM5FixedPointQ18.h"

// Exact CPU positions in Q18 centimetres. RendererErrorQ18 is a per-axis
// outward bound supplied by the caller, not a measured renderer guarantee.
struct VOXELLANDALGO_API FVoxelLandM5FixedTriangleQ18
{
	FVoxelLandM5FixedPointQ18 A;
	FVoxelLandM5FixedPointQ18 B;
	FVoxelLandM5FixedPointQ18 C;
	FVoxelLandM5FixedPointQ18 RendererErrorQ18;
	int32 SourceTriangle = 0;
};
