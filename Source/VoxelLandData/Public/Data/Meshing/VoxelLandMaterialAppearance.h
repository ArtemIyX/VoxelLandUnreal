#pragma once

#include "CoreMinimal.h"
#include "Data/Meshing/VoxelLandResolvedMaterialRule.h"

struct FVoxelLandMaterialAppearance
{
	static constexpr uint8 MaxSlots = 32;
	static constexpr uint8 FallbackSlot = 0;

	TArray<FVoxelLandResolvedMaterialRule> Rules;
	uint8 SlotCount = 1;
};
