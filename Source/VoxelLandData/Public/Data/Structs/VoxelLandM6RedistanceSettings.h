#pragma once

#include "CoreMinimal.h"
#include "VoxelLandM6RedistanceSettings.generated.h"

USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM6RedistanceSettings
{
	GENERATED_BODY()

	static constexpr uint32 CurrentVersion = 1;
	static constexpr uint8 MaxBandVoxels = 2;

	UPROPERTY()
	uint32 Version = CurrentVersion;

	UPROPERTY(EditAnywhere, Category = "Re-Distance")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, Category = "Re-Distance", meta = (EditCondition = "bEnabled", ClampMin = "1", ClampMax = "2"))
	uint8 BandVoxels = 1;
};
