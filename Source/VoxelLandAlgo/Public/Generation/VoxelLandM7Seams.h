#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandChunkClassification.h"
#include "Data/Structs/VoxelLandChunkKey.h"
#include "Data/Structs/VoxelLandError.h"
#include "Data/Structs/VoxelLandM7NormalAnchor.h"
#include "Data/Structs/VoxelLandM7NormalFrameBatch.h"
#include "Data/Structs/VoxelLandMeshData.h"

// BuildM7SeamMesh records the positive owner layer with the same vertex buffer.
// Only Terrain.Indices is an ordinary draw. Candidate indices are topology input.
struct FVoxelLandM7SeamMesh
{
	FVoxelLandChunkKey Key;
	int64 BaseVoxelSizeCm = 0;
	FVoxelLandMeshData Terrain;
	TArray<uint32> PositiveOwnerIndices;
	// X-fastest 33^3 lower endpoint, then edge axis. Same ID for both halves.
	TArray<uint32> PositiveOwnerEdges;
	EVoxelLandChunkClassification Classification = EVoxelLandChunkClassification::NeedsSamples;
};

struct FVoxelLandM7VerticalFace
{
	uint8 Axis = 0; // X or Y
	bool bPositiveFace = true;
	// Zero uses one coarse voxel. A positive override may reduce that bound.
	double MaxDepthCm = 0.0;
};

struct FVoxelLandM7CurtainEnd
{
	FVector Top = FVector::ZeroVector;
	FVector Bottom = FVector::ZeroVector;
	bool bPresent = false;
};

struct FVoxelLandM7SeamPatch
{
	FVoxelLandMeshData CoarseTerrain;
	// Same order as the supplied children. Each stream uses that child's origin.
	TArray<FVoxelLandMeshData> FineTerrain;
	// Render-only stream with the coarse origin. Never feed to collision.
	FVoxelLandMeshData Curtain;
	FVoxelLandM7CurtainEnd Start;
	FVoxelLandM7CurtainEnd End;
	double MaxDepthCm = 0.0;
	uint32 SharedNormalFallbacks = 0;
};

namespace VoxelLandM7Seams
{
// Builds a single complete 2:1 vertical patch. Children may arrive in any order.
// Caller uses the same admitted field for the terrain and the M7.2 frame evaluator.
// Exterior incident patches must agree on corner intervals before publication.
// All streams are replaced together on success. Failure leaves OutPatch unchanged.
VOXELLANDALGO_API FVoxelLandError BuildVerticalFace(const FVoxelLandM7SeamMesh& Coarse,
	TConstArrayView<FVoxelLandM7SeamMesh> Fine, const FVoxelLandM7VerticalFace& Face,
	TFunctionRef<FVoxelLandError(TConstArrayView<FVoxelLandM7NormalAnchor>, FVoxelLandM7NormalFrameBatch&)> EvaluateFrames,
	TFunctionRef<bool()> IsCancelled, FVoxelLandM7SeamPatch& OutPatch);
}
