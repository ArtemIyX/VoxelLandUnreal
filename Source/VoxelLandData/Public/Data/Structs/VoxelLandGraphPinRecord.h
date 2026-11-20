#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandGraphValueType.h"
#include "VoxelLandGraphPinRecord.generated.h"

USTRUCT()
struct VOXELLANDDATA_API FVoxelLandGraphPinRecord
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid Id;

	UPROPERTY()
	FString Name;

	UPROPERTY()
	EVoxelLandGraphValueType Type = EVoxelLandGraphValueType::Float;

	UPROPERTY()
	bool bOutput = false;

	UPROPERTY()
	float FloatDefault = 0.0f;

	UPROPERTY()
	int32 IntDefault = 0;

	UPROPERTY()
	bool BoolDefault = false;
};
