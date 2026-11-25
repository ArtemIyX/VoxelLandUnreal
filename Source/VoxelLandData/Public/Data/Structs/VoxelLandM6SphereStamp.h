#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandM6SphereOperation.h"

struct FVoxelLandM6SphereStamp
{
	FGuid Id;
	EVoxelLandM6SphereOperation Operation = EVoxelLandM6SphereOperation::Add;
	FIntVector CenterCm = FIntVector::ZeroValue;
	int32 RadiusCm = 0;
};
