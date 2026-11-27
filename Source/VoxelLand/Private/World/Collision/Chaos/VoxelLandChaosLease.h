#pragma once

#include "Data/Field/Chunks/VoxelLandChunkData.h"

struct FVoxelLandChaosLease
{
	FGuid FieldOwner;
	TSet<FVoxelLandChunkKey> Tiles;
	TSet<FVoxelLandChunkKey> Pinned;
};
