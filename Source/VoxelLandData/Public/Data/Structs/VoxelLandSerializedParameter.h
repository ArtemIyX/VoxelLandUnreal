#pragma once

#include "CoreMinimal.h"
#include "VoxelLandSerializedParameter.generated.h"

USTRUCT()
struct VOXELLANDDATA_API FVoxelLandSerializedParameter
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid Id;

	UPROPERTY()
	FString Name;

	UPROPERTY()
	uint8 Type = 0;

	UPROPERTY()
	float FloatDefault = 0.f;

	UPROPERTY()
	int32 IntDefault = 0;

};
