#pragma once

#include "Data/Structs/VoxelLandM6FilterSettings.h"

struct FVoxelLandM6FilterPlan
{
	int32 Halo = 0;
	uint8 ActiveStages = 0;
	uint8 OutputHalo[FVoxelLandM6FilterSettings::MaxStages] = {};

	bool HasStage(int32 Index) const { return (ActiveStages & (1 << Index)) != 0; }
};
