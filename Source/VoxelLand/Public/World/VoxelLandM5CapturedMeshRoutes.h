#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"

class FVoxelLandM5BudgetLease;
class FVoxelLandM5CandidateResult;
class FVoxelLandM5CapturedScratchChunks;
class FVoxelLandM5ScratchMesh;

// Source-bound route proposals, not support, clearance, complete coverage,
// renderer, final geometry, source freshness, or publication certification.
class VOXELLAND_API FVoxelLandM5CapturedMeshRoutes final
{
public:
	// Worker thread only. Every search column must remain strictly inside the
	// meshed chunk rectangle by two voxels, and search depth is bounded in cm.
	// Exact source, mesh and budget are mandatory. Caller remains responsible
	// for omitted mesh owners, renderer and provider lifetime. Failure or
	// cancellation preserves Out.
	static FVoxelLandM5Diagnostic Build(
		TSharedRef<const FVoxelLandM5CapturedScratchChunks, ESPMode::ThreadSafe> Source,
		TSharedRef<const FVoxelLandM5ScratchMesh, ESPMode::ThreadSafe> Mesh, int64 MaxSearchDepthCm,
		TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5CapturedMeshRoutes, ESPMode::ThreadSafe>& Out);
	// CPU route check only; no publication or final-field authority.
	FVoxelLandM5Diagnostic CheckSupport(int64 ContactSkinCm, int32 MaxTotalFaces, TFunctionRef<bool()> IsCancelled) const;
	const FVoxelLandM5CapturedScratchChunks& GetSource() const { return *Source; }
	const FVoxelLandM5ScratchMesh& GetMesh() const { return *Mesh; }
	const FVoxelLandM5CandidateResult& GetCandidate() const { return *Candidate; }
private:
	FVoxelLandM5CapturedMeshRoutes(TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease,
		TSharedRef<const FVoxelLandM5CapturedScratchChunks, ESPMode::ThreadSafe> InSource,
		TSharedRef<const FVoxelLandM5ScratchMesh, ESPMode::ThreadSafe> InMesh)
		: Lease(MoveTemp(InLease)), Source(MoveTemp(InSource)), Mesh(MoveTemp(InMesh)) {}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TSharedRef<const FVoxelLandM5CapturedScratchChunks, ESPMode::ThreadSafe> Source;
	TSharedRef<const FVoxelLandM5ScratchMesh, ESPMode::ThreadSafe> Mesh;
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate;
};
