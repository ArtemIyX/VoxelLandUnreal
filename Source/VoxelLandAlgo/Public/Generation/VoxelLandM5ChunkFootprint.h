#pragma once

#include "CoreMinimal.h"

struct FVoxelLandChunkKey;
struct FVoxelLandM5ChunkFootprint;

namespace VoxelLand::M5::ChunkFootprint
{
// LOD0, positive whole-centimetre voxel size. Smoothing adds one sample on
// every side; slope adds one height-sampling position on each XY side. These
// bounds do not establish provider support, mesh ownership or world validity.
// Failure leaves Out unchanged. This bounded operation needs no cancellation.
VOXELLANDALGO_API bool TryBuild(const FVoxelLandChunkKey& Key, int64 VoxelSizeCm, bool bSmooth, bool bNormalizeSlope,
	FVoxelLandM5ChunkFootprint& Out);
}
