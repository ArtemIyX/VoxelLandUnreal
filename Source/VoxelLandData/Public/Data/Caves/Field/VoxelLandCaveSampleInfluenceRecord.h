#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Field/VoxelLandCaveLatticePoint.h"
#include "Data/Caves/Field/VoxelLandCaveOwnedLatticeEdge.h"

// Conservative inverse dependencies only; not evidence of changed geometry.
struct FVoxelLandM5SampleInfluence
{
	static constexpr int32 CellCount = 8;
	static constexpr int32 EdgeCount = 54;
	FVoxelLandM5LatticePoint Cells[CellCount];
	FVoxelLandM5OwnedLatticeEdge Edges[EdgeCount];
};
