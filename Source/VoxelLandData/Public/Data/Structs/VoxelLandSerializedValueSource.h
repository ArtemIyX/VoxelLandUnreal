#pragma once

#include "CoreMinimal.h"
#include "VoxelLandSerializedValueSource.generated.h"

USTRUCT()
struct VOXELLANDDATA_API FVoxelLandSerializedValueSource
{
	GENERATED_BODY()

	UPROPERTY()
	bool bParameter = false;

	UPROPERTY()
	int32 Index = 0;

};
