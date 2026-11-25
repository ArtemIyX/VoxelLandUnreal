#pragma once

#include "CoreMinimal.h"
#include "Data/Graph/Schema/VoxelLandGraphValueType.h"
#include "Data/Program/Operators/VoxelLandOperatorStage.h"
#include "VoxelLandExternalOperator.generated.h"

// Canonical, provider-independent bytecode table entry. No executable pointers are serialized.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandExternalOperator
{
	GENERATED_BODY()
	UPROPERTY()
	FGuid Id;
	UPROPERTY()
	int32 Version = 0;
	UPROPERTY()
	EVoxelLandOperatorStage Stage = EVoxelLandOperatorStage::Sample;
	UPROPERTY()
	TArray<EVoxelLandGraphValueType> Inputs;
	UPROPERTY()
	EVoxelLandGraphValueType Output = EVoxelLandGraphValueType::Float;
	UPROPERTY()
	TArray<uint8> Configuration;
	UPROPERTY()
	int32 SeedSlot = 0;
};
