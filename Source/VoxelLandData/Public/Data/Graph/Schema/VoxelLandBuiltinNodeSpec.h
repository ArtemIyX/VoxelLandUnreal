#pragma once

#include "CoreMinimal.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Program/Operators/VoxelLandOperatorStage.h"
#include "Data/Graph/Schema/VoxelLandBuiltinPinSpec.h"

// Value-owned runtime description. It contains no UObject or editor presentation hooks.
struct VOXELLANDDATA_API FVoxelLandBuiltinNodeSpec
{
	EVoxelLandBuiltinNode Kind = EVoxelLandBuiltinNode::DensityOutput;
	FGuid OperatorId;
	int32 OperatorVersion = 1;
	EVoxelLandOperatorStage Stage = EVoxelLandOperatorStage::Sample;
	bool bParameterReference = false;
	bool bNoise = false;
	bool bTypedCompilerDispatch = false;
	int32 NoiseDimensions = 0;
	int32 MinVariadicInputs = 0;
	int32 MaxVariadicInputs = 0;
	TArray<FVoxelLandBuiltinPinSpec> Pins;
};
