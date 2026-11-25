#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandM6StampOperation.h"
#include "Data/Enums/VoxelLandM6StampShape.h"

struct FVoxelLandM6Stamp
{
	FGuid Id;
	EVoxelLandM6StampShape Shape = EVoxelLandM6StampShape::Sphere;
	EVoxelLandM6StampOperation Operation = EVoxelLandM6StampOperation::Add;
	FInt64Vector OriginCm = FInt64Vector(0);
	// Only capsule uses SegmentEndCm. Only box uses HalfExtentCm.
	FInt64Vector SegmentEndCm = FInt64Vector(0);
	FIntVector HalfExtentCm = FIntVector::ZeroValue;
	int32 RadiusCm = 0;
	int32 BlendRadiusCm = 0;
};
