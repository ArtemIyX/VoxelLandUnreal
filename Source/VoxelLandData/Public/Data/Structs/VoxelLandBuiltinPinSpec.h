#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandGraphValueType.h"

// An authoring pin's stable name, type and exact default literal.
struct VOXELLANDDATA_API FVoxelLandBuiltinPinSpec
{
    FString Name;
    EVoxelLandGraphValueType Type = EVoxelLandGraphValueType::Float;
    bool bOutput = false;
    bool bUniform = false;
    FString DefaultLiteral;
};
