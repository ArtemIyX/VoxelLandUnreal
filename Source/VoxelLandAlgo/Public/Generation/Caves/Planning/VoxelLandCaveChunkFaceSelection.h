#pragma once

#include "CoreMinimal.h"

// Face index in the freshly generated CPU mesh of the named LOD-zero chunk.
struct VOXELLANDALGO_API FVoxelLandM5ChunkFaceSelection
{
	FIntVector ChunkCoordinate = FIntVector::ZeroValue;
	int32 FaceIndex = INDEX_NONE;
};
