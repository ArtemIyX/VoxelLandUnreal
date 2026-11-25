#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM7NormalFrame.h"

struct FVoxelLandM7NormalFrameBatch
{
	TArray<FVoxelLandM7NormalFrame> Frames;
	uint32 FallbackCount = 0;
};
