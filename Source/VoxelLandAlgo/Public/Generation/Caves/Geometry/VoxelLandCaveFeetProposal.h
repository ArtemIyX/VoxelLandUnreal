#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Geometry/VoxelLandCavePointCm.h"

// Uncertified straight feet path in logical centimetres. It provides no floor,
// route connectivity, surface, protection, or final-mesh guarantee.
struct VOXELLANDALGO_API FVoxelLandM5FeetProposal
{
	FVoxelLandM5PointCm StartFeetCm;
	FVoxelLandM5PointCm EndFeetCm;
};
