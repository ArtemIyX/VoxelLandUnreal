#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandGraphValueType.h"
#include "VoxelLandGraphParameter.generated.h"

// IDs, rather than display names or positions, identify parameters in settings and nodes.
USTRUCT(BlueprintType)
struct VOXELLANDDATA_API FVoxelLandGraphParameter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Parameter")
	FGuid Id;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	FString Name;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	EVoxelLandGraphValueType Type = EVoxelLandGraphValueType::Float;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	float FloatDefault = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	int32 IntDefault = 0;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool BoolDefault = false;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bHasRange = false;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	float FloatMin = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	float FloatMax = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	int32 IntMin = 0;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	int32 IntMax = 1;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	FText Tooltip;
};
