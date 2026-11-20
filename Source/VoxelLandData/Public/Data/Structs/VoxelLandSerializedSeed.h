#pragma once

#include "CoreMinimal.h"
#include "VoxelLandSerializedSeed.generated.h"

USTRUCT()
struct VOXELLANDDATA_API FVoxelLandSerializedSeed
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid NodeId;

	UPROPERTY()
	int32 Salt = 0;

};
