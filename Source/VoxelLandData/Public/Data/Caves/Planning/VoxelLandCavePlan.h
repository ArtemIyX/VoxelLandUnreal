#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Geometry/VoxelLandCaveBounds.h"
#include "Data/Caves/Planning/VoxelLandCaveChamber.h"
#include "Data/Caves/Planning/VoxelLandCaveConnection.h"
#include "Data/Caves/Planning/VoxelLandCaveEntrance.h"
#include "Data/Caves/Planning/VoxelLandCaveJunction.h"
#include "Data/Caves/Planning/VoxelLandCavePortal.h"
#include "Data/Caves/Planning/VoxelLandCaveRegionCoord.h"
#include "Data/Caves/Geometry/VoxelLandCaveRoute.h"
#include "Data/Caves/Geometry/VoxelLandCaveSegment.h"

// Scratch until all validation and indexing succeed; publish only by shared const reference.
// Feature IDs are plan-scoped: in regional mode, callers must include the signed
// Region coordinates with the local ID when deriving streams or comparing features
// across plans. This is transient topology, not a persisted descriptor or plan format.
struct FVoxelLandM5CavePlan
{
	// Every primitive profile must match this version; mixed plans fail closed.
	int32 GeometryVersion = 1;
	FVoxelLandM5Bounds Bounds;
	FVoxelLandM5RegionCoord Region;
	TArray<FVoxelLandM5Chamber> Chambers;
	TArray<FVoxelLandM5Connection> Connections;
	TArray<FVoxelLandM5Segment> Segments;
	TArray<FVoxelLandM5Entrance> Entrances;
	TArray<FVoxelLandM5Junction> Junctions;
	TArray<FVoxelLandM5Portal> Portals;
	TArray<FVoxelLandM5Route> Routes;
};
