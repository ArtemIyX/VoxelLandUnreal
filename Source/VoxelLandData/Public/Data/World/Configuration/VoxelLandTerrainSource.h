#pragma once

#include "VoxelLandTerrainSource.generated.h"

UENUM(BlueprintType)
enum class EVoxelLandTerrainSource : uint8
{
	HeightTerrain UMETA(DisplayName = "Height Terrain"),
	DirectDensity UMETA(DisplayName = "Direct Density")
};
