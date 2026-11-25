#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Geometry/VoxelLandCavePointCm.h"
#include "VoxelLandCaveBounds.generated.h"

// Bounds are half-open on all axes. An empty/default box is invalid until configured.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM5Bounds
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "Bounds", meta = (ToolTip = "Inclusive minimum in logical world centimetres."))
	FVoxelLandM5PointCm MinInclusive;
	UPROPERTY(EditAnywhere, Category = "Bounds", meta = (ToolTip = "Exclusive maximum in logical world centimetres."))
	FVoxelLandM5PointCm MaxExclusive;
};
