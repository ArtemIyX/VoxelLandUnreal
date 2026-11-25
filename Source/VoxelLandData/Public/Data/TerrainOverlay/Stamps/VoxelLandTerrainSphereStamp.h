#pragma once

#include "CoreMinimal.h"
#include "Data/TerrainOverlay/Stamps/VoxelLandTerrainSphereOperation.h"

struct FVoxelLandM6SphereStamp
{
	FGuid Id;
	EVoxelLandM6SphereOperation Operation = EVoxelLandM6SphereOperation::Add;
	FIntVector CenterCm = FIntVector::ZeroValue;
	int32 RadiusCm = 0;
};
