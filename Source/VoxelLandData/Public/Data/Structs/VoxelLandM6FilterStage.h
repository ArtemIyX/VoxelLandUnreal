#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandM6FilterKind.h"

struct FVoxelLandM6FilterStage
{
	FGuid Id;
	EVoxelLandM6FilterKind Kind = EVoxelLandM6FilterKind::Smooth;
	FInt64Vector MinCm = FInt64Vector(0);
	FInt64Vector MaxCm = FInt64Vector(0);
	uint16 Strength = MAX_uint16;
};
