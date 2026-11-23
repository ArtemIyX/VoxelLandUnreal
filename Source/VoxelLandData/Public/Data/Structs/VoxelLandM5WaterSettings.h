#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandM5WaterPolicy.h"
#include "VoxelLandM5WaterSettings.generated.h"

// DryWorld is an authoritative owner declaration for the entire generated world.
// The owner must ensure no water exists there. Other policies do not declare a dry world.
// This record does not simulate or inspect water.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM5WaterSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "Water",
		meta = (ToolTip = "DryWorld declares the entire generated world water-free; other policies do not certify dry caves."))
	EVoxelLandM5WaterPolicy Policy = EVoxelLandM5WaterPolicy::Ignore;
	UPROPERTY(EditAnywhere, Category = "Water", meta = (ToolTip = "Logical world water level in centimetres."))
	int64 LevelCm = 0;
};
