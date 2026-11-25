#pragma once

#include "CoreMinimal.h"
#include "VoxelLandTerrainFilterKind.generated.h"

UENUM()
enum class EVoxelLandM6FilterKind : uint8
{
	Smooth = 0,
	Flatten = 1,
	MakeSpace = 2
};
