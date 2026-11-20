#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandInstruction.h"
#include "Data/Structs/VoxelLandExternalOperator.h"
#include "Data/Structs/VoxelLandNoiseConfig.h"
#include "Data/Structs/VoxelLandParameterSlot.h"
#include "Data/Structs/VoxelLandSeedSlot.h"
#include "Data/Structs/VoxelLandSmoothFilter.h"

struct FVoxelLandProgramDraft
{
	uint32 FormatVersion = 1;
	int32 RegisterCount = 0;
	TArray<FVoxelLandInstruction> Instructions;
	TArray<float> Constants;
	TArray<FVoxelLandParameterSlot> Parameters;
	TArray<FVoxelLandSeedSlot> Seeds;
	TArray<FVoxelLandNoiseConfig> Noise;
	TArray<FVoxelLandExternalOperator> ExternalOperators;
	FVoxelLandSmoothFilter SmoothFilter;
};
