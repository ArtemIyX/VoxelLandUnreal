#pragma once

#include "CoreMinimal.h"

struct FVoxelLandMaterialBlend
{
	static constexpr int32 LayerCount = 4;
	static constexpr int32 WeightSum = MAX_uint8;

	uint8 Slots[LayerCount] = {};
	uint8 Weights[LayerCount] = {};
};
