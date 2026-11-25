#pragma once

#include "CoreMinimal.h"
#include "Data/Program/Serialization/VoxelLandSerializedValueSource.h"
#include "VoxelLandSerializedSmoothFilter.generated.h"

USTRUCT()
struct VOXELLANDDATA_API FVoxelLandSerializedSmoothFilter
{
	GENERATED_BODY()
	UPROPERTY()
	bool bPresent = false;
	UPROPERTY()
	FVoxelLandSerializedValueSource Enabled;
	UPROPERTY()
	FVoxelLandSerializedValueSource Strength;
};
