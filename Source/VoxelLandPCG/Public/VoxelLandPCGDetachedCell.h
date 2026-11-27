#pragma once

#include "CoreMinimal.h"

class FVoxelLandPCGWorkFence;

struct FVoxelLandPCGDetachedCell
{
	TSharedPtr<FVoxelLandPCGWorkFence, ESPMode::ThreadSafe> Work;
	int64 FieldBytes = 0;
};
