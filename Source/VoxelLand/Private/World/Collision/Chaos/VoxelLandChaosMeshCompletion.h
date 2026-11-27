#pragma once

#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Data/Meshing/VoxelLandChaosMesh.h"

struct FVoxelLandChaosMeshCompletion
{
	FVoxelLandChunkKey Key;
	uint64 Ticket = 0;
	TSharedPtr<const FVoxelLandChaosMesh, ESPMode::ThreadSafe> Mesh;
};
