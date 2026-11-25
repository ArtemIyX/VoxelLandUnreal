#pragma once

#include "CoreMinimal.h"
#include "VoxelLandCaveCapsuleProfile.generated.h"

// Full standing height includes the hemispherical capsule ends.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM5CapsuleProfile
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "Clearance",
		meta = (ClampMin = "1", ToolTip = "Full standing capsule height, including end caps, in centimetres."))
	int32 StandingHeightCm = 185;
	UPROPERTY(EditAnywhere, Category = "Clearance", meta = (ClampMin = "1", ToolTip = "Capsule radius in centimetres."))
	int32 RadiusCm = 35;
	UPROPERTY(EditAnywhere, Category = "Clearance",
		meta = (ClampMin = "0", ClampMax = "90", ToolTip = "Maximum traversable floor angle in degrees."))
	int32 MaxSlopeDegrees = 45;
	UPROPERTY(EditAnywhere, Category = "Clearance",
		meta = (ClampMin = "0", ToolTip = "Maximum floor step height in centimetres."))
	int32 MaxStepCm = 45;
	UPROPERTY(EditAnywhere, Category = "Clearance",
		meta = (ClampMin = "0", ToolTip = "Additional clearance beyond the capsule in centimetres."))
	int32 ClearanceMarginCm = 10;
};
