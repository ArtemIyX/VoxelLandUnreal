#pragma once

#include "CoreMinimal.h"
#include "Generation/Collision/VoxelLandCollisionField.h"

struct FVoxelLandTerrainPlacementInput
{
	const FVoxelLandCollisionField& Field;
	TConstArrayView<FVoxelLandCollisionField::FChunkPtr> Chunks;
	const FBox& Bounds;
};
