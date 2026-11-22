#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Bounds.h"
#include "Data/Structs/VoxelLandM5Chamber.h"
#include "Data/Structs/VoxelLandM5Connection.h"
#include "Data/Structs/VoxelLandM5Entrance.h"
#include "Data/Structs/VoxelLandM5Junction.h"
#include "Data/Structs/VoxelLandM5Portal.h"
#include "Data/Structs/VoxelLandM5RegionCoord.h"
#include "Data/Structs/VoxelLandM5Route.h"
#include "Data/Structs/VoxelLandM5Segment.h"

// Scratch until all validation and indexing succeed; publish only by shared const reference.
// Feature IDs are plan-scoped: in regional mode, callers must include the signed
// Region coordinates with the local ID when deriving streams or comparing features
// across plans. This is transient topology, not a persisted descriptor or plan format.
struct VOXELLANDDATA_API FVoxelLandM5CavePlan
{
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
