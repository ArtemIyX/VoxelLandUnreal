#pragma once

#include "CoreMinimal.h"
#include "Data/Program/Execution/VoxelLandInstruction.h"
#include "Data/Program/Operators/VoxelLandExternalOperator.h"
#include "Data/Field/Execution/VoxelLandNoiseConfig.h"
#include "Data/Program/Execution/VoxelLandParameterSlot.h"
#include "Data/Program/Execution/VoxelLandSeedSlot.h"
#include "Data/Program/Execution/VoxelLandSmoothFilter.h"

struct FVoxelLandProgramDraft
{
	// Increment whenever graph compiler or current execution semantics change.
	static constexpr uint32 CompilerSemanticVersion = 4;
	static constexpr uint32 CurrentFormatVersion = 2;
	int32 ExternalFilterIndex = INDEX_NONE;
	uint32 FormatVersion = CurrentFormatVersion;
	int32 RegisterCount = 0;
	TArray<FVoxelLandInstruction> Instructions;
	TArray<float> Constants;
	TArray<FVoxelLandParameterSlot> Parameters;
	TArray<FVoxelLandSeedSlot> Seeds;
	TArray<FVoxelLandNoiseConfig> Noise;
	TArray<FVoxelLandExternalOperator> ExternalOperators;
	FVoxelLandSmoothFilter SmoothFilter;
};
