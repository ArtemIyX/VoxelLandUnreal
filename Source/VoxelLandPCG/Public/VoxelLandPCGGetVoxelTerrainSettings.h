#pragma once

#include "PCGSettings.h"

#include "VoxelLandPCGGetVoxelTerrainSettings.generated.h"

UCLASS(BlueprintType, ClassGroup = (Procedural))
class VOXELLANDPCG_API UVoxelLandPCGGetVoxelTerrainSettings final : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("GetVoxelTerrain"); }
	virtual FText GetDefaultNodeTitle() const override
	{
		return NSLOCTEXT("VoxelLandPCG", "GetVoxelTerrain", "Get Voxel Terrain");
	}
	virtual FText GetNodeTooltipText() const override
	{
		return NSLOCTEXT(
			"VoxelLandPCG", "GetVoxelTerrainTooltip", "Reads the complete terrain snapshot for this VoxelLand cell.");
	}
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spatial; }
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};
