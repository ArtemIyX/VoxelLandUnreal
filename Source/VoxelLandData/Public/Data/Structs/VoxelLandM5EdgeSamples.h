#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5LatticePoint.h"

// The complete samples read by the four cells incident to an owned edge.
struct FVoxelLandM5EdgeSamples
{
	static constexpr int32 Count = 18;
	FVoxelLandM5LatticePoint Samples[Count];
};
