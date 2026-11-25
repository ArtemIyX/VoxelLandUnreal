#pragma once

#include "Data/Structs/VoxelLandChunkKey.h"

struct FVoxelLandM7ClipmapFace
{
	FVoxelLandChunkKey Coarse;
	// The first tangential axis varies fastest, with axes taken in ascending order.
	FVoxelLandChunkKey Fine[4];
	uint8 Axis = 0;
	int8 Sign = 1;
};
