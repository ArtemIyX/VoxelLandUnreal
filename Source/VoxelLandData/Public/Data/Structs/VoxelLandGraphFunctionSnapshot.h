#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandGraphValueType.h"
#include "Data/Structs/VoxelLandGraphLinkRecord.h"
#include "Data/Structs/VoxelLandGraphNodeRecord.h"
#include "Data/Structs/VoxelLandGraphParameter.h"

// Immutable, UObject-free copy of a single function dependency.
struct VOXELLANDDATA_API FVoxelLandGraphFunctionSnapshot
{
    FString AssetPath;
    int32 SourceVersion = 1;
    TArray<FVoxelLandGraphParameter> Inputs;
    EVoxelLandGraphValueType OutputType = EVoxelLandGraphValueType::Float;
    FGuid OutputNode;
    FGuid OutputPin;
    TArray<FVoxelLandGraphNodeRecord> Nodes;
    TArray<FVoxelLandGraphLinkRecord> Links;
};
