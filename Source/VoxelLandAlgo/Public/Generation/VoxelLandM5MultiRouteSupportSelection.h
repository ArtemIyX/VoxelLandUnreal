#pragma once

#include "CoreMinimal.h"
#include "Generation/VoxelLandM5ChunkFaceSelection.h"

// One route interval's support faces, identified by production-mesh owner.
// Indices refer to the CPU meshes freshly generated from the supplied chunks.
struct VOXELLANDALGO_API FVoxelLandM5MultiRouteSupportSelection
{
	uint64 RouteId = 0;
	int32 LegIndex = INDEX_NONE;
	TArray<FVoxelLandM5ChunkFaceSelection> SupportFaces;
};
