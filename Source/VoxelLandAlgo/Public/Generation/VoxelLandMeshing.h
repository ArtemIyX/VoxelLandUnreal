#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandChunkClassification.h"
#include "Data/Enums/VoxelLandMeshResult.h"
#include "Data/Structs/VoxelLandChunkData.h"
#include "Data/Structs/VoxelLandMeshData.h"
#include "Data/Structs/VoxelLandMeshingScratch.h"

struct FVoxelLandM7SeamMesh;

namespace VoxelLandMeshing
{
// No current VM interval bounds are proven; unknown bounds must be sampled.
VOXELLANDALGO_API EVoxelLandChunkClassification ClassifyBeforeSampling();
VOXELLANDALGO_API EVoxelLandChunkClassification Classify(const FVoxelLandChunkData& Chunk);
// VoxelSizeCm is the world's base spacing. The mesher derives level spacing
// from Chunk.Key.LOD while keeping the canonical base-density units.
// Cancellation is checked between slices. Output is only assigned on success.
VOXELLANDALGO_API EVoxelLandMeshResult BuildSurfaceNets(const FVoxelLandChunkData& Chunk, float VoxelSizeCm,
	FVoxelLandMeshingScratch& Scratch, FVoxelLandMeshData& OutMesh, TFunctionRef<bool()> IsCancelled);
// Expanded precision envelope is reserved for validated chunk-local M4.
VOXELLANDALGO_API EVoxelLandMeshResult BuildSurfaceNets(const FVoxelLandChunkData& Chunk, float VoxelSizeCm,
	FVoxelLandMeshingScratch& Scratch, FVoxelLandMeshData& OutMesh, TFunctionRef<bool()> IsCancelled,
	bool bUnlimitedM4);
// Explicit opt-in. Positive owner candidates share the ordinary vertex buffer
// and never enter the ordinary draw index stream.
VOXELLANDALGO_API EVoxelLandMeshResult BuildM7SeamMesh(const FVoxelLandChunkData& Chunk, int64 BaseVoxelSizeCm,
	FVoxelLandMeshingScratch& Scratch, FVoxelLandM7SeamMesh& OutMesh, TFunctionRef<bool()> IsCancelled,
	bool bUnlimitedM4 = false);
}
