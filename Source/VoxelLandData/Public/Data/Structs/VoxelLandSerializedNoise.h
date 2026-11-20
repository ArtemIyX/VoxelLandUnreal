#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandSerializedValueSource.h"
#include "VoxelLandSerializedNoise.generated.h"

USTRUCT()
struct VOXELLANDDATA_API FVoxelLandSerializedNoise
{
	GENERATED_BODY()

	UPROPERTY()
	int32 SeedSlot = 0;

	UPROPERTY()
	FVoxelLandSerializedValueSource Frequency;

	UPROPERTY()
	FVoxelLandSerializedValueSource Octaves;

	UPROPERTY()
	FVoxelLandSerializedValueSource Gain;

	UPROPERTY()
	FVoxelLandSerializedValueSource Lacunarity;

};
