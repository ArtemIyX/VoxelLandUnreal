#pragma once

#include "CoreMinimal.h"
#include "Data/Meshing/VoxelLandMaterialRuleInput.h"

struct FVoxelLandResolvedMaterialRule
{
	uint8 Slot = 0;
	EVoxelLandMaterialRuleInput Input = EVoxelLandMaterialRuleInput::ElevationCm;
	double Threshold = 0.0;
	double BlendWidth = 0.0;
};
