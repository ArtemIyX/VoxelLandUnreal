#pragma once

#include "Data/Structs/VoxelLandM6FilterSettings.h"

struct FVoxelLandM6FilterPlan
{
	int32 Halo = 0;
	uint8 ActiveStages = 0;
	uint8 RedistanceBand = 0;
	uint8 OutputHalo[FVoxelLandM6FilterSettings::MaxStages] = {};

	bool IsActive() const { return ActiveStages != 0 || RedistanceBand > 0; }
	bool HasStage(int32 Index) const { return (ActiveStages & (1 << Index)) != 0; }
	static int32 FilterRadius(const FVoxelLandM6FilterStage& Stage)
	{
		return Stage.Kind == EVoxelLandM6FilterKind::MakeSpace || Stage.Strength == 0 ? 0 : 1;
	}
};
