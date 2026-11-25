#pragma once

#include "CoreMinimal.h"
#include "Data/Graph/Schema/VoxelLandGraphValueType.h"
#include "Data/Graph/Sources/VoxelLandGraphLinkRecord.h"
#include "Data/Graph/Sources/VoxelLandGraphNodeRecord.h"
#include "Data/Graph/Sources/VoxelLandGraphParameter.h"
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
