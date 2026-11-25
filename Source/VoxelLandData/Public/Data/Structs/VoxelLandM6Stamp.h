#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandM6StampOperation.h"
#include "Data/Enums/VoxelLandM6StampShape.h"
#include "VoxelLandM6Stamp.generated.h"

USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM6Stamp
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Stamp", meta = (ToolTip = "A unique, stable, non-zero ID within the stamp list."))
	FGuid Id;

	UPROPERTY(EditAnywhere, Category = "Stamp")
	EVoxelLandM6StampShape Shape = EVoxelLandM6StampShape::Sphere;

	UPROPERTY(EditAnywhere, Category = "Stamp", meta = (ToolTip = "Add solid terrain or subtract a clearing from it."))
	EVoxelLandM6StampOperation Operation = EVoxelLandM6StampOperation::Add;

	UPROPERTY(EditAnywhere, Category = "Stamp", meta = (ToolTip = "Global logical centimetres. Capsule uses this as its first endpoint."))
	FInt64Vector OriginCm = FInt64Vector(0);

	UPROPERTY(EditAnywhere, Category = "Stamp", meta = (EditCondition = "Shape == EVoxelLandM6StampShape::Capsule", EditConditionHides))
	FInt64Vector SegmentEndCm = FInt64Vector(0);

	UPROPERTY(EditAnywhere, Category = "Stamp", meta = (EditCondition = "Shape == EVoxelLandM6StampShape::Box", EditConditionHides))
	FIntVector HalfExtentCm = FIntVector::ZeroValue;

	UPROPERTY(EditAnywhere, Category = "Stamp", meta = (EditCondition = "Shape != EVoxelLandM6StampShape::Box", EditConditionHides, ClampMin = "1", ClampMax = "2000000"))
	int32 RadiusCm = 0;

	UPROPERTY()
	int32 BlendRadiusCm = 0;
};
