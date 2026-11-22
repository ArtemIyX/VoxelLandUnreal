#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandM5WaterPolicy.h"
#include "VoxelLandM5WaterSettings.generated.h"

// Classification policy only; no water or fluid simulation is implied.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM5WaterSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "Water",
		meta = (ToolTip = "Policy for planned caves and pockets below the water level."))
	EVoxelLandM5WaterPolicy Policy = EVoxelLandM5WaterPolicy::Ignore;
	UPROPERTY(EditAnywhere, Category = "Water", meta = (ToolTip = "Logical world water level in centimetres."))
	int64 LevelCm = 0;
};
