#pragma once

#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "World/Collision/Chaos/VoxelLandChaosCook.h"

struct FVoxelLandChaosCookCompletion
{
	FVoxelLandChunkKey Key;
	uint64 Ticket = 0;
	FVoxelLandChaosCookResultPtr Result;
};
