#pragma once

#include "CoreMinimal.h"

struct FVoxelLandMaterialCandidates
{
	static constexpr int32 MaxCount = 2;

	uint8 Slots[MaxCount] = {};
	uint8 Weights[MaxCount] = {};
	int32 Count = 0;
};
