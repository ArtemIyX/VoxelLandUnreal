#pragma once

#include "CoreMinimal.h"
#include "VoxelLandChaosCollisionOverrideMode.generated.h"

UENUM(BlueprintType)
enum class EVoxelLandChaosCollisionOverrideMode : uint8
{
	Inherit,
	Disabled,
	Enabled
};
