#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5PointCm.h"

// Intended support plane, described by three logical-world centimetre points.
// The ordering specifies orientation. The validator rejects degenerate points,
// non-walkable slopes and any unsupported interval; this is not a certificate.
struct VOXELLANDDATA_API FVoxelLandM5SupportPlane
{
	FVoxelLandM5PointCm A;
	FVoxelLandM5PointCm B;
	FVoxelLandM5PointCm C;
};
