#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandNoiseFamily.h"

namespace VoxelLandNative
{
struct FConfig
{
	EVoxelLandNoiseFamily Family = EVoxelLandNoiseFamily::Perlin;
	bool bUnitScale = false;
	int32 Octaves = 3;
	float Gain = 0.5f;
	float Lacunarity = 2.0f;
};
}
