#pragma once

#include "Data/TerrainOverlay/Configuration/VoxelLandTerrainOverlay.h"
#include "Engine/DataAsset.h"
#include "VoxelLandTerrainOverlayConfig.generated.h"

class UVoxelLandM4WorldConfig;

// A generation-time overlay linked to a cave-disabled M4 source asset.
UCLASS(BlueprintType)
class VOXELLANDDATA_API UVoxelLandM6WorldConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(FPrimaryAssetType(TEXT("VoxelLandM6World")), GetFName());
	}

	UPROPERTY(EditAnywhere, Category = "VoxelLand",
		meta = (ToolTip = "Existing finite or erosion-disabled local M4 configuration, with M5 caves disabled."))
	TObjectPtr<UVoxelLandM4WorldConfig> Baseline;

	UPROPERTY(EditAnywhere, Category = "VoxelLand", meta = (ShowOnlyInnerProperties))
	FVoxelLandM6WorldOverlay Overlay;
};
