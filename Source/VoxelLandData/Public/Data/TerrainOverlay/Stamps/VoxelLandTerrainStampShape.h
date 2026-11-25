#pragma once

#include "CoreMinimal.h"
#include "VoxelLandTerrainStampShape.generated.h"

UENUM()
enum class EVoxelLandM6StampShape : uint8
{
	Sphere = 0,
	Box = 1,
	Capsule = 2
};
