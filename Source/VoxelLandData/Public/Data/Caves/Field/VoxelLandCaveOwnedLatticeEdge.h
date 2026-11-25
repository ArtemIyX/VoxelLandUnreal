#pragma once

#include "CoreMinimal.h"
#include "Data/Field/Chunks/VoxelLandChunkKey.h"
#include "Data/Caves/Field/VoxelLandCaveLatticePoint.h"

// Axis is 0, 1 or 2; Lower is the owned edge's global lower endpoint.
struct FVoxelLandM5OwnedLatticeEdge
{
	FVoxelLandM5LatticePoint Lower;
	FVoxelLandChunkKey Owner;
	uint8 Axis = 0;
};
