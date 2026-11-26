#pragma once

#include "CoreMinimal.h"
#include "Data/Meshing/VoxelLandMaterialRuleInput.h"
#include "VoxelLandMaterialRule.generated.h"

class UVoxelLandSurfaceMaterial;

USTRUCT()
struct VOXELLANDDATA_API FVoxelLandMaterialRule
{
	GENERATED_BODY()

#if WITH_EDITORONLY_DATA
	UPROPERTY(
		EditAnywhere, Category = "Rule", meta = (ToolTip = "Choose a surface from this setup's material palette."))
	TObjectPtr<UVoxelLandSurfaceMaterial> Surface;
#endif

	UPROPERTY(VisibleAnywhere, Category = "Rule")
	FGuid SurfaceId;

	UPROPERTY(EditAnywhere, Category = "Rule")
	EVoxelLandMaterialRuleInput Input = EVoxelLandMaterialRuleInput::ElevationCm;

	UPROPERTY(
		EditAnywhere, Category = "Rule", meta = (ToolTip = "Elevation in centimeters or slope in degrees from up."))
	double Threshold = 0.0;

	UPROPERTY(EditAnywhere, Category = "Rule",
		meta = (ClampMin = "0.0", ToolTip = "Full transition width centered on the threshold. Zero is a hard cutoff."))
	double BlendWidth = 0.0;
};
