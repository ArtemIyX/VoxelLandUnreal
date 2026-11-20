#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandValueSource.h"

struct FVoxelLandNoiseConfig
{
	uint32 SeedSlot = 0;
	FVoxelLandValueSource Frequency;  // positive float
	FVoxelLandValueSource Octaves;    // int parameter, or integral float constant, 1..16
	FVoxelLandValueSource Gain;       // float, 0..1
	FVoxelLandValueSource Lacunarity; // float, >=1
};
