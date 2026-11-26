#pragma once

#include "CoreMinimal.h"

class FVoxelLandGenerator;
class FVoxelLandWorkerContext;
struct FVoxelLandChunkData;
struct FVoxelLandChunkKey;
struct FVoxelLandError;
struct FVoxelLandMeshData;

class VOXELLANDALGO_API FVoxelLandTerrainAmbientOcclusion final
{
public:
	static constexpr uint8 FullyVisible = 255;

	// Sample the same unscaled base lattice at every LOD. The optional LOD0 data must belong to this job.
	// Unsupported fields leave an empty stream. Errors preserve the input mesh and its lighting stream.
	[[nodiscard]] static FVoxelLandError Bake(FVoxelLandMeshData& Mesh, const FVoxelLandGenerator& Generator,
		FVoxelLandWorkerContext& Worker, const FVoxelLandChunkKey& ChunkKey, const FVector3d& LogicalOrigin,
		TFunctionRef<bool()> IsCancelled, const FVoxelLandChunkData* LOD0Data = nullptr);

	// Pure kernel. The sampler returns the completed signed field at an integer base-lattice point,
	// in 1/1024 voxel units. LogicalOrigin and mesh positions are measured in centimeters.
	[[nodiscard]] static FVoxelLandError Bake(FVoxelLandMeshData& Mesh, float BaseVoxelSizeCm,
		const FVector3d& LogicalOrigin,
		TFunctionRef<FVoxelLandError(const FInt64Vector&, int16&)> SampleCompletedLattice,
		TFunctionRef<bool()> IsCancelled);
};
