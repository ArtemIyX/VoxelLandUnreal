#pragma once

#include "CoreMinimal.h"
#include "VoxelLandCurveAuthoringKey.generated.h"

// Normalized input in [0,65535], output in 1/256 cm.
USTRUCT(BlueprintType)
struct VOXELLANDDATA_API FVoxelLandM4CurveAuthoringKey
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Curve", meta = (ClampMin = "0", ClampMax = "65535"))
	int32 InputQ16 = 0;

	UPROPERTY(EditAnywhere, Category = "Curve", meta = (ClampMin = "-512000000", ClampMax = "512000000"))
	int32 OutputQ8 = 0;
};
