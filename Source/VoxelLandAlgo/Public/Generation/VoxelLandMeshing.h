#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandChunkData.h"
#include "Data/Structs/VoxelLandMeshData.h"

struct FVoxelLandMeshingScratch
{
    TArray<int32> CellVertices;
};

enum class EVoxelLandMeshResult : uint8 { Success, InvalidInput, Cancelled };

namespace VoxelLandMeshing
{
    // No current VM interval bounds are proven; unknown bounds must be sampled.
    VOXELLANDALGO_API EVoxelLandChunkClassification ClassifyBeforeSampling();
    VOXELLANDALGO_API EVoxelLandChunkClassification Classify(const FVoxelLandChunkData& Chunk);
    // Cancellation is checked between slices. Output is only assigned on success.
    VOXELLANDALGO_API EVoxelLandMeshResult BuildSurfaceNets(const FVoxelLandChunkData& Chunk, float VoxelSizeCm,
        FVoxelLandMeshingScratch& Scratch, FVoxelLandMeshData& OutMesh, TFunctionRef<bool()> IsCancelled);
}
