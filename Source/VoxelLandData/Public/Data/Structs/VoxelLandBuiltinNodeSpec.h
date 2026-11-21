#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/Enums/VoxelLandOperatorStage.h"
#include "Data/Structs/VoxelLandBuiltinPinSpec.h"

// Value-owned runtime description. It contains no UObject or editor presentation hooks.
struct VOXELLANDDATA_API FVoxelLandBuiltinNodeSpec
{
    EVoxelLandBuiltinNode Kind = EVoxelLandBuiltinNode::DensityOutput;
    FGuid OperatorId;
    int32 OperatorVersion = 1;
    EVoxelLandOperatorStage Stage = EVoxelLandOperatorStage::Sample;
    bool bParameterReference = false;
    bool bNoise = false;
    int32 MinVariadicInputs = 0;
    int32 MaxVariadicInputs = 0;
    TArray<FVoxelLandBuiltinPinSpec> Pins;
};
