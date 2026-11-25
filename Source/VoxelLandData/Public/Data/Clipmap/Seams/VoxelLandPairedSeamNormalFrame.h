#pragma once

#include "CoreMinimal.h"
#include "Data/Clipmap/Seams/VoxelLandSeamNormalFrame.h"

struct FVoxelLandM7PairedNormalFrame
{
	int32 FirstVertex = INDEX_NONE;
	int32 SecondVertex = INDEX_NONE;
	FVoxelLandM7NormalFrame Frame;
};
