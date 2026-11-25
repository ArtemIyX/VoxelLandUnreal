#pragma once

#include "CoreMinimal.h"
#include "VoxelLandCavePointCm.generated.h"

// Logical world coordinates in whole centimetres; independent of origin rebasing.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM5PointCm
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "Position", meta = (ToolTip = "Logical world X in centimetres."))
	int64 X = 0;
	UPROPERTY(EditAnywhere, Category = "Position", meta = (ToolTip = "Logical world Y in centimetres."))
	int64 Y = 0;
	UPROPERTY(EditAnywhere, Category = "Position", meta = (ToolTip = "Logical world Z in centimetres."))
	int64 Z = 0;
};
