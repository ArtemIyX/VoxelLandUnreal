#pragma once

#include "CoreMinimal.h"
#include "VoxelLandM5NoisePocketSettings.generated.h"

// Independent optional local cavities; these do not promise connectivity or clearance.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM5NoisePocketSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "Pockets", meta = (ToolTip = "Enable optional local noise cavities."))
	bool bEnabled = false;
	UPROPERTY(
		EditAnywhere, Category = "Pockets", meta = (ClampMin = "1", ToolTip = "Noise lattice spacing in centimetres."))
	int32 LatticeSpacingCm = 2000;
	UPROPERTY(EditAnywhere, Category = "Pockets",
		meta = (ClampMin = "0", ClampMax = "65535", ToolTip = "Unsigned normalized noise threshold, from 0 to 65535."))
	int32 ThresholdQ16 = 50000;
	UPROPERTY(EditAnywhere, Category = "Pockets",
		meta = (ClampMin = "0", ToolTip = "Minimum depth below the proven surface in centimetres."))
	int32 MinDepthCm = 1000;
	UPROPERTY(EditAnywhere, Category = "Pockets",
		meta = (ClampMin = "0", ToolTip = "Maximum depth below the proven surface in centimetres."))
	int32 MaxDepthCm = 10000;
	UPROPERTY(
		EditAnywhere, Category = "Pockets", meta = (ToolTip = "Allow local pockets to intersect planned cave air."))
	bool bAllowAttachment = true;
	UPROPERTY(EditAnywhere, Category = "Pockets", meta = (ToolTip = "Allow isolated local pocket volumes."))
	bool bAllowEnclosedVoids = true;
	UPROPERTY(EditAnywhere, Category = "Pockets", meta = (ToolTip = "Independent noise seed salt."))
	int64 Salt = 0;
};
