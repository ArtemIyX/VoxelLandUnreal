#pragma once

#include "CoreMinimal.h"
#include "VoxelLandM6FilterKind.generated.h"

UENUM()
enum class EVoxelLandM6FilterKind : uint8
{
	Smooth = 0,
	Flatten = 1,
	MakeSpace = 2
};
