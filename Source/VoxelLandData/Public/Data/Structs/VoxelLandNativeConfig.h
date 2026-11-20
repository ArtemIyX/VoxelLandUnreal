#pragma once

#include "CoreMinimal.h"

namespace VoxelLandNative
{
struct FConfig
{
	bool bFbm = false;
	int32 Octaves = 3;
	float Gain = 0.5f;
	float Lacunarity = 2.0f;
};
}
