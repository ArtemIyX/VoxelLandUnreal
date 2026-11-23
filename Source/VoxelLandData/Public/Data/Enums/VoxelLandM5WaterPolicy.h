#pragma once

#include "CoreMinimal.h"
#include "VoxelLandM5WaterPolicy.generated.h"

UENUM()
enum class EVoxelLandM5WaterPolicy : uint8
{
	Ignore,
	RejectFlooded,
	AllowAndMark,
	// Authoritative world input: no water exists anywhere in the declared world.
	DryWorld
};
