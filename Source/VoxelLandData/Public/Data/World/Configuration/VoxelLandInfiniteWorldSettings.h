#pragma once

#include "CoreMinimal.h"
#include "VoxelLandInfiniteWorldSettings.generated.h"

// Regional ownership is finite per request; the world has no global prepass.
USTRUCT(BlueprintType)
struct VOXELLANDDATA_API FVoxelLandInfiniteWorldSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "Regions",
		meta = (ClampMin = "1", ToolTip = "Width of one horizontal planning region in centimetres."))
	int64 RegionSizeCm = 204800;
	UPROPERTY(
		EditAnywhere, Category = "Regions", meta = (ToolTip = "Logical X origin of the regional grid in centimetres."))
	int64 RegionOriginXCm = 0;
	UPROPERTY(
		EditAnywhere, Category = "Regions", meta = (ToolTip = "Logical Y origin of the regional grid in centimetres."))
	int64 RegionOriginYCm = 0;
	UPROPERTY(EditAnywhere, Category = "Regions",
		meta = (ToolTip = "Inclusive lower regional planning height in centimetres."))
	int64 MinZCm = -819200;
	UPROPERTY(EditAnywhere, Category = "Regions",
		meta = (ToolTip = "Exclusive upper regional planning height in centimetres."))
	int64 MaxZCm = 819200;
};
