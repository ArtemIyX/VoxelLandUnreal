#pragma once
#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveHandles.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Generation/Caves/Geometry/VoxelLandCaveFixedTriangleQ18.h"
#include "Generation/Caves/Planning/VoxelLandCaveChunkFaceSelection.h"
#include "Templates/Function.h"

class FVoxelLandM5BoundBaseline;
class FVoxelLandM5InfluenceSet;
class FVoxelLandM5BudgetLease;
class FVoxelLandWorkerContext;

// Real baseline CPU mesh for exactly the requested finite owner rectangle.
// No exterior completeness, renderer, final-field or publication proof.
class VOXELLANDALGO_API FVoxelLandM5BaselineMesh final
{
public:
	~FVoxelLandM5BaselineMesh();
	FVoxelLandM5BaselineMesh(const FVoxelLandM5BaselineMesh&) = delete;
	FVoxelLandM5BaselineMesh& operator=(const FVoxelLandM5BaselineMesh&) = delete;
	static FVoxelLandM5Diagnostic Build(TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> Baseline,
		TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> Influence, FVoxelLandM5BudgetRef SourceBudget,
		FVoxelLandWorkerContext& Worker, FIntVector MinChunk, FIntVector MaxChunkExclusive, int32 MaxChunks,
		int32 MaxTriangles, FVoxelLandM5BudgetRef JobBudget, TFunctionRef<bool()> IsCancelled,
		TVoxelLandM5ConstPtr<FVoxelLandM5BaselineMesh>& Out);
	bool Matches(const FVoxelLandM5BoundBaseline& InBaseline, const FVoxelLandM5InfluenceSet& InInfluence,
		const FVoxelLandM5Budget& InSourceBudget, const FVoxelLandM5Budget& InJobBudget) const;
	FIntVector GetMinInclusive() const { return MinInclusive; }
	FIntVector GetMaxExclusive() const { return MaxExclusive; }
	TConstArrayView<FVoxelLandM5FixedTriangleQ18> GetTriangles() const { return Triangles; }
	TConstArrayView<FVoxelLandM5ChunkFaceSelection> GetFaces() const { return Faces; }

private:
	FVoxelLandM5BaselineMesh(TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> InBaseline,
		TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> InInfluence, FVoxelLandM5BudgetRef InSourceBudget,
		FVoxelLandM5BudgetRef InJobBudget, TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease);
	TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> Baseline;
	TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> Influence;
	FVoxelLandM5BudgetRef SourceBudget, JobBudget;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	FIntVector MinInclusive, MaxExclusive;
	TArray<FVoxelLandM5FixedTriangleQ18> Triangles;
	TArray<FVoxelLandM5ChunkFaceSelection> Faces;
};
