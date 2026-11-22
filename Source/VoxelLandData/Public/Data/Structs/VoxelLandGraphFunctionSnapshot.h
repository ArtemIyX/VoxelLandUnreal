#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandGraphValueType.h"
#include "Data/Structs/VoxelLandGraphLinkRecord.h"
#include "Data/Structs/VoxelLandGraphNodeRecord.h"
#include "Data/Structs/VoxelLandGraphParameter.h"
#include "VoxelLandGraphFunctionSnapshot.generated.h"

// Immutable, UObject-free copy of a single function dependency.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandGraphFunctionSnapshot
{
	GENERATED_BODY()
	UPROPERTY()
	FString AssetPath;
	UPROPERTY()
	int32 SourceVersion = 1;
	UPROPERTY()
	TArray<FVoxelLandGraphParameter> Inputs;
	UPROPERTY()
	EVoxelLandGraphValueType OutputType = EVoxelLandGraphValueType::Float;
	UPROPERTY()
	FGuid OutputNode;
	UPROPERTY()
	FGuid OutputPin;
	UPROPERTY()
	TArray<FVoxelLandGraphNodeRecord> Nodes;
	UPROPERTY()
	TArray<FVoxelLandGraphLinkRecord> Links;
};
