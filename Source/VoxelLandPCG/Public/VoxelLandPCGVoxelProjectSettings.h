#pragma once

#include "PCGSettings.h"

#include "VoxelLandPCGVoxelProjectSettings.generated.h"

UCLASS(BlueprintType, ClassGroup = (Procedural))
class VOXELLANDPCG_API UVoxelLandPCGVoxelProjectSettings final : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Projection", meta = (PCG_Overridable))
	FVector Direction = FVector::DownVector;

	UPROPERTY(EditAnywhere, Category = "Projection",
		meta = (ClampMin = "1.0", ClampMax = "100000.0", Units = "cm", PCG_Overridable))
	double MaximumDistanceCm = 2000.0;

	UPROPERTY(EditAnywhere, Category = "Projection", meta = (PCG_Overridable))
	bool bGroundOnly = true;

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelProject"); }
	virtual FText GetDefaultNodeTitle() const override
	{
		return NSLOCTEXT("VoxelLandPCG", "VoxelProject", "Voxel Project");
	}
	virtual FText GetNodeTooltipText() const override
	{
		return NSLOCTEXT("VoxelLandPCG", "VoxelProjectTooltip",
			"Projects candidates onto known voxel terrain along a bounded direction.");
	}
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spatial; }
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};
