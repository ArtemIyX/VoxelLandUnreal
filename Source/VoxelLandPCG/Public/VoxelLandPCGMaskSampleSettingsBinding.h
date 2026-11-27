#pragma once

#include "CoreMinimal.h"

#include "VoxelLandPCGMaskSampleSettingsBinding.generated.h"

USTRUCT(BlueprintType)
struct FVoxelLandPCGMaskSampleSettingsBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	FGuid OutputId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	FName Attribute = NAME_None;
};
