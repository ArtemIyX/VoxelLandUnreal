#pragma once

#include "CoreMinimal.h"

struct FVoxelLandTerrainPlacementPoint
{
	FVector Position = FVector::ZeroVector;
	FVector Normal = FVector::UpVector;
	uint32 Seed = 0;
};
