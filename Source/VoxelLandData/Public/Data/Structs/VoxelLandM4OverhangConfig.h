#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM4OverhangSettings.h"

// Immutable runtime settings for version-1 3D Perlin displacement.
// Frequency is cycles per centimeter in (0, 1]. Seed identity is stable per node.
struct FVoxelLandM4OverhangConfig
{
	FVoxelLandM4OverhangSettings Displacement;
	float FrequencyPerCm = 0.001f;
	FGuid NodeId = FGuid(0x564c4d34, 1, 0, 1);
	uint32 Salt = 0;
};
