#pragma once

#include "CoreMinimal.h"
#include "Data/Meshing/VoxelLandMeshResult.h"

struct FVoxelLandChunkData;
struct FVoxelLandChaosMesh;

class VOXELLANDALGO_API FVoxelLandChaosMeshing final
{
public:
	static constexpr uint8 MaxLod = 2;

	// Builds the collision for one logical LOD 0 footprint. Empty is a successful result.
	// Output stays unchanged on failure or cancellation.
	[[nodiscard]] static EVoxelLandMeshResult Build(const FVoxelLandChunkData& Chunk, float VoxelSizeCm,
		uint8 RequestedLod, FVoxelLandChaosMesh& OutMesh, TFunctionRef<bool()> IsCancelled,
		bool bUnlimitedHeightCoordinates = false);
};
