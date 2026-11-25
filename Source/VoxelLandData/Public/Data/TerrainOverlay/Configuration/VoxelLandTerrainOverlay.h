#pragma once

#include "CoreMinimal.h"
#include "Data/TerrainOverlay/Filters/VoxelLandTerrainFilterStage.h"
#include "Data/TerrainOverlay/Redistance/VoxelLandTerrainRedistanceSettings.h"
#include "Data/TerrainOverlay/Stamps/VoxelLandTerrainStamp.h"
#include "VoxelLandTerrainOverlay.generated.h"

struct FVoxelLandGeneratorSettings;
struct FVoxelLandM6FilterSettings;
struct FVoxelLandM6StampSettings;

// Reflected asset fields and immutable by-value worker capture use the same records.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM6WorldOverlay
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Stamps")
	bool bStampsEnabled = true;

	UPROPERTY(EditAnywhere, Category = "Stamps",
		meta = (EditCondition = "bStampsEnabled",
			ToolTip = "Up to 16 hard stamps. Entries run in array order before region filters."))
	TArray<FVoxelLandM6Stamp> Stamps;

	UPROPERTY(EditAnywhere, Category = "Filters")
	bool bFiltersEnabled = true;

	UPROPERTY(EditAnywhere, Category = "Filters",
		meta = (EditCondition = "bFiltersEnabled",
			ToolTip = "Up to 8 filters. Entries run in array order after stamps."))
	TArray<FVoxelLandM6FilterStage> Filters;

	UPROPERTY(EditAnywhere, Category = "Re-Distance",
		meta = (ToolTip = "Optional final near-surface approximation. Its halo counts toward the eight-voxel total."))
	FVoxelLandM6RedistanceSettings Redistance;

	bool IsActive() const;
	FVoxelLandM6StampSettings GetStamps() const;
	FVoxelLandM6FilterSettings GetFilters() const;
	void ApplyTo(FVoxelLandGeneratorSettings& Settings) const;
};
