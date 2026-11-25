#pragma once

#include "CoreMinimal.h"
#include "Data/Clipmap/Seams/VoxelLandSeamNormalFrame.h"

struct FVoxelLandM7NormalFrameBatch
{
	TArray<FVoxelLandM7NormalFrame> Frames;
	uint32 FallbackCount = 0;
};
