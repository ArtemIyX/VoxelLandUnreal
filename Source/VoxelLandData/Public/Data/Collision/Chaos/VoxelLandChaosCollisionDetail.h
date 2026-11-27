#pragma once

#include "CoreMinimal.h"
#include "VoxelLandChaosCollisionDetail.generated.h"

UENUM(BlueprintType)
enum class EVoxelLandChaosCollisionDetail : uint8
{
	Disabled,
	LOD0 UMETA(DisplayName = "LOD 0"),
	LOD1 UMETA(DisplayName = "LOD 1"),
	LOD2 UMETA(DisplayName = "LOD 2")
};
