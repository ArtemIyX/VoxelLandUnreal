#pragma once

#include "Data/Clipmap/Planning/VoxelLandClipmapFace.h"
#include "Data/Clipmap/Planning/VoxelLandClipmapSettings.h"

struct FVoxelLandM7ClipmapLayout
{
	FVoxelLandM7ClipmapSettings Settings;
	TArray<FVoxelLandChunkKey> Roots;
	// Nearest logical leaf bounds first, with stable full-key ties.
	TArray<FVoxelLandChunkKey> Leaves;
	TSet<FVoxelLandChunkKey> SplitParents;
	TArray<FVoxelLandM7ClipmapFace> MixedFaces;
};
