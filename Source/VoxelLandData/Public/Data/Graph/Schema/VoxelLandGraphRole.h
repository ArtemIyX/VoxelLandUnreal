#pragma once

#include "CoreMinimal.h"
#include "VoxelLandGraphRole.generated.h"

UENUM(BlueprintType)
enum class EVoxelLandGraphRole : uint8
{
	Terrain,
	Material
};
