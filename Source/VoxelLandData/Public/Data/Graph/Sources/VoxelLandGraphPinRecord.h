#pragma once

#include "CoreMinimal.h"
#include "Data/Graph/Schema/VoxelLandGraphValueType.h"
#include "VoxelLandGraphPinRecord.generated.h"

USTRUCT()
struct VOXELLANDDATA_API FVoxelLandGraphPinRecord
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid Id;

	// Stable function interface identity; invalid for ordinary node pins.
	UPROPERTY()
	FGuid InterfaceId;

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

	// Retains an invalid editor literal as an explicit compilation failure.
	UPROPERTY()
	bool bInvalidDefault = false;
};
