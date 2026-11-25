#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Templates/Function.h"

class FVoxelLandM5BudgetLease;
class FVoxelLandM5CapturedBaseline;
class FVoxelLandM5CapturedFiniteCandidate;
class FVoxelLandM5ScratchChunks;

// Source-bound composed scratch, not complete influence, final safety, source
// freshness, terrain publication, or a cave certificate.
class VOXELLAND_API FVoxelLandM5CapturedScratchChunks final
{
public:
	// Worker-thread calls. Network uses the candidate's exact retained source;
	// pocket-only accepts only a disabled network and enabled pockets. Failure
	// or cancellation preserves Out. Caller retains provider/module admission.
	static FVoxelLandM5Diagnostic BuildNetwork(
		TSharedRef<const FVoxelLandM5CapturedFiniteCandidate, ESPMode::ThreadSafe> Candidate,
		const FIntVector& MinChunk, const FIntVector& MaxChunk, int32 MaxChunks, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5CapturedScratchChunks, ESPMode::ThreadSafe>& Out);
	static FVoxelLandM5Diagnostic BuildPocketOnly(
		TSharedRef<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe> Source, const FIntVector& MinChunk,
		const FIntVector& MaxChunk, int32 MaxChunks, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5CapturedScratchChunks, ESPMode::ThreadSafe>& Out);
	const FVoxelLandM5CapturedBaseline& GetSource() const { return *Source; }
	const FVoxelLandM5CapturedFiniteCandidate* GetCandidate() const { return Candidate.Get(); }
	const FVoxelLandM5ScratchChunks& GetChunks() const { return *Chunks; }
	TSharedRef<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> GetChunksShared() const
	{
		return Chunks.ToSharedRef();
	}

private:
	static FVoxelLandM5Diagnostic BuildInternal(
		TSharedRef<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe> Source,
		TSharedPtr<const FVoxelLandM5CapturedFiniteCandidate, ESPMode::ThreadSafe> Candidate,
		const FIntVector& MinChunk, const FIntVector& MaxChunk, int32 MaxChunks, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5CapturedScratchChunks, ESPMode::ThreadSafe>& Out);
	FVoxelLandM5CapturedScratchChunks(TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease,
		TSharedRef<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe> InSource)
		: Lease(MoveTemp(InLease))
		, Source(MoveTemp(InSource))
	{
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TSharedRef<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe> Source;
	TSharedPtr<const FVoxelLandM5CapturedFiniteCandidate, ESPMode::ThreadSafe> Candidate;
	TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Chunks;
};
