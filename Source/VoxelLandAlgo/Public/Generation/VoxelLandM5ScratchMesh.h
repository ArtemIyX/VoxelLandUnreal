#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Generation/VoxelLandM5FixedTriangleQ18.h"
#include "Generation/VoxelLandM5ChunkFaceSelection.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;
class FVoxelLandM5ScratchChunks;

// Exact CPU Q18 triangles from a checked rectangle of scratch chunks.
// This does not establish source freshness, complete influence or neighbor
// ownership, renderer transforms, support, clearance, or publication safety.
// Zero renderer error on returned records describes the CPU-coordinate input
// only; it is not evidence that a renderer has zero uncertainty.
class VOXELLANDALGO_API FVoxelLandM5ScratchMesh final
{
public:
	// Same-budget immutable finite LOD-zero source. MaxChunks is 1..16 and
	// MaxTriangles 1..196608 for the complete result. Preflight counts the
	// two potential faces per sign-changing owned edge before reserving output
	// and per-chunk triangle scratch; vertex scratch remains worst-case bounded.
	// Mesher and adapter run synchronously; failure preserves Out. Caller owns
	// source provenance, provider/module lifetime and allocator overhead.
	static FVoxelLandM5Diagnostic Build(TSharedRef<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Source,
		int32 MaxChunks, int32 MaxTriangles, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5ScratchMesh, ESPMode::ThreadSafe>& Out);
	const FVoxelLandM5ScratchChunks& GetSource() const { return *Source; }
	TConstArrayView<FVoxelLandM5FixedTriangleQ18> GetTriangles() const { return Triangles; }
	// One source chunk and original mesher face per aligned triangle; records
	// are ordered by chunk Z/Y/X and then original face index.
	TConstArrayView<FVoxelLandM5ChunkFaceSelection> GetFaces() const { return Faces; }
	bool IsOwnedBy(const FVoxelLandM5Budget& Budget) const;
private:
	FVoxelLandM5ScratchMesh(TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease,
		TSharedRef<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> InSource)
		: Lease(MoveTemp(InLease)), Source(MoveTemp(InSource)) {}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TSharedRef<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Source;
	TArray<FVoxelLandM5FixedTriangleQ18> Triangles;
	TArray<FVoxelLandM5ChunkFaceSelection> Faces;
};
