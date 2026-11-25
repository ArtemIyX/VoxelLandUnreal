#pragma once

#include "CoreMinimal.h"
#include "VoxelLandWorldKind.generated.h"

// A world definition chooses this domain through its concrete asset type.
UENUM(BlueprintType)
enum class EVoxelLandWorldKind : uint8
{
	Finite,
	Infinite,
};
