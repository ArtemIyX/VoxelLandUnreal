#pragma once

#include "CoreMinimal.h"
#include "Data/Graph/Schema/VoxelLandGraphValueType.h"
#include "VoxelLandParameterOverride.generated.h"

USTRUCT(BlueprintType)
struct VOXELLANDDATA_API FVoxelLandParameterOverride
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Override")
	FGuid ParameterId;

	UPROPERTY(EditAnywhere, Category = "Override")
	EVoxelLandGraphValueType Type = EVoxelLandGraphValueType::Float;

	UPROPERTY(EditAnywhere, Category = "Override")
	float FloatValue = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Override")
	int32 IntValue = 0;

	UPROPERTY(EditAnywhere, Category = "Override")
	bool BoolValue = false;
};
