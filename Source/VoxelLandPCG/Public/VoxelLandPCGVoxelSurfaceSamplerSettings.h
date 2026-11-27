#pragma once

#include "PCGSettings.h"

#include "VoxelLandPCGVoxelSurfaceSamplerSettings.generated.h"

UCLASS(BlueprintType, ClassGroup = (Procedural))
class VOXELLANDPCG_API UVoxelLandPCGVoxelSurfaceSamplerSettings final : public UPCGSettings
{
	GENERATED_BODY()

public:
	virtual bool UseSeed() const override { return true; }

	UPROPERTY(EditAnywhere, Category = "Sampling", meta = (ClampMin = "10.0", Units = "cm", PCG_Overridable))
	double MinimumSpacingCm = 600.0;

	UPROPERTY(EditAnywhere, Category = "Sampling",
		meta = (ClampMin = "0.0", ClampMax = "89.9", Units = "deg", PCG_Overridable))
	double MaximumSlopeDegrees = 55.0;

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelSurfaceSampler"); }
	virtual FText GetDefaultNodeTitle() const override
	{
		return NSLOCTEXT("VoxelLandPCG", "VoxelSurfaceSampler", "Voxel Surface Sampler");
	}
	virtual FText GetNodeTooltipText() const override
	{
		return NSLOCTEXT("VoxelLandPCG", "VoxelSurfaceSamplerTooltip",
			"Samples grounded points on the canonical LOD 0 terrain surface.");
	}
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spatial; }
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};
