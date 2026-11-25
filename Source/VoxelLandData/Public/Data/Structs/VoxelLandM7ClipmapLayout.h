#pragma once

#include "Data/Structs/VoxelLandM7ClipmapFace.h"
#include "Data/Structs/VoxelLandM7ClipmapSettings.h"

struct FVoxelLandM7ClipmapLayout
{
	FVoxelLandM7ClipmapSettings Settings;
	TArray<FVoxelLandChunkKey> Roots;
	// Nearest logical leaf bounds first, with stable full-key ties.
	TArray<FVoxelLandChunkKey> Leaves;
	TSet<FVoxelLandChunkKey> SplitParents;
	TArray<FVoxelLandM7ClipmapFace> MixedFaces;
};
