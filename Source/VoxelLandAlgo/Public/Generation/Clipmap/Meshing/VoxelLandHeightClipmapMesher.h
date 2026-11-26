#pragma once

#include "CoreMinimal.h"
#include "Data/Field/Chunks/VoxelLandError.h"

class FVoxelLandGenerator;
struct FVoxelLandChunkKey;
struct FVoxelLandMeshData;

class VOXELLANDALGO_API FVoxelLandHeightClipmapMesher final
{
public:
	// Coarse tiles insert matching samples along the indicated finer edges.
	// Bits 0–3 are -X, +X, -Y, +Y. Tile Z is zero and height is in world centimeters.
	static FVoxelLandError Build(const FVoxelLandChunkKey& Key, uint8 CoarseEdgeMask,
		const FVoxelLandGenerator& Generator, TFunctionRef<bool()> IsCancelled, FVoxelLandMeshData& OutMesh);
};
