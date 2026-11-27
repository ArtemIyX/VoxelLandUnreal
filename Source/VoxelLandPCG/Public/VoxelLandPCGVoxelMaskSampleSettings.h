#pragma once

#include "PCGSettings.h"
#include "VoxelLandPCGMaskSampleSettingsBinding.h"

#include "VoxelLandPCGVoxelMaskSampleSettings.generated.h"

UCLASS(BlueprintType, ClassGroup = (Procedural))
class VOXELLANDPCG_API UVoxelLandPCGVoxelMaskSampleSettings final : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Masks")
	TArray<FVoxelLandPCGMaskSampleSettingsBinding> Bindings;

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelMaskSample"); }
	virtual FText GetDefaultNodeTitle() const override
	{
		return NSLOCTEXT("VoxelLandPCG", "VoxelMaskSample", "Voxel Mask Sample");
	}
	virtual FText GetNodeTooltipText() const override
	{
		return NSLOCTEXT(
			"VoxelLandPCG", "VoxelMaskSampleTooltip", "Writes named mask weights onto voxel terrain points.");
	}
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spatial; }
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};
