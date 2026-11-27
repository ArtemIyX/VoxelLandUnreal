#pragma once

#include "Engine/EngineTypes.h"
#include "VoxelLandChaosCollisionResponse.generated.h"

USTRUCT(BlueprintType)
struct VOXELLANDDATA_API FVoxelLandChaosCollisionResponse
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision")
	FName Channel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Collision")
	TEnumAsByte<ECollisionResponse> Response = ECR_Block;
};
