#pragma once

#include "CoreMinimal.h"
#include "Data/Meshing/VoxelLandMeshData.h"

struct FVoxelLandTerrainPlacementMesh
{
	FVoxelLandMeshData Geometry;
	FVector Origin = FVector::ZeroVector;
};
