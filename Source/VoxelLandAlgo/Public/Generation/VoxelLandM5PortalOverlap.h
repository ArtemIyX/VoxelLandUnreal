#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5PointCm.h"

// Borrowed immutable, X-fastest global sample block. The owner must retain and
// account for the storage for the entire handshake.
struct VOXELLANDALGO_API FVoxelLandM5PortalOverlap
{
	FVoxelLandM5PointCm OriginCm;
	FIntVector Counts = FIntVector::ZeroValue;
	int32 SpacingCm = 0;
	TConstArrayView<int16> Samples;
};
