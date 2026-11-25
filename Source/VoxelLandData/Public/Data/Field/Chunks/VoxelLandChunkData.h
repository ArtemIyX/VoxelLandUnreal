#pragma once

#include "CoreMinimal.h"
#include "Data/Field/Chunks/VoxelLandChunkKey.h"

struct FVoxelLandChunkData
{
	static constexpr int32 Cells = 32;
	static constexpr int32 SamplesPerAxis = 35;
	static constexpr int32 SampleCount = 35 * 35 * 35;
	FVoxelLandChunkKey Key;
	TArray<int16> Density; // X fastest, then Y, then Z; units of 1/1024 voxel.
	uint64 Hash = 0;
};
