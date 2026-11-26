#pragma once

#include "CoreMinimal.h"
#include "Data/Meshing/VoxelLandResolvedMaterialRule.h"

class FVoxelLandMaterialGraphProgram;
class IVoxelLandMaterialSignalSource;

struct FVoxelLandMaterialAppearance
{
	static constexpr uint8 MaxSlots = 32;
	static constexpr uint8 FallbackSlot = 0;

	TArray<FVoxelLandResolvedMaterialRule> Rules;
	uint8 SlotCount = 1;
	uint64 Seed = 0;
	TSharedPtr<const FVoxelLandMaterialGraphProgram, ESPMode::ThreadSafe> Program;
	TSharedPtr<const IVoxelLandMaterialSignalSource, ESPMode::ThreadSafe> Signals;
};
