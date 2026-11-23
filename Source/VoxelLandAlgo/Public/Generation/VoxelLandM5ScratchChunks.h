#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandChunkData.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Data/Structs/VoxelLandM5StageManifest.h"
#include "Templates/Function.h"

struct FVoxelLandM5CaveSettings;
class FVoxelLandM5BaselineWindow;
class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;
class FVoxelLandM5CandidateResult;
class FVoxelLandM5ProtectionGrid;

// Scratch chunks share a single composed lattice. This is not a certificate,
// production generator output, or permission to publish terrain.
class VOXELLANDALGO_API FVoxelLandM5ScratchChunks final
{
public:
	FVoxelLandM5ScratchChunks(const FVoxelLandM5ScratchChunks&) = delete;
	FVoxelLandM5ScratchChunks& operator=(const FVoxelLandM5ScratchChunks&) = delete;
	TConstArrayView<FVoxelLandChunkData> GetChunks() const { return Chunks; }
	FIntVector GetMinInclusive() const { return MinInclusive; }
	FIntVector GetMaxExclusive() const { return MaxExclusive; }
	int32 GetVoxelSizeCm() const { return VoxelSizeCm; }
	bool IsOwnedBy(const FVoxelLandM5Budget& Budget) const;

	// Exact immediate input identities and all manifest versions, not proof of
	// source freshness, complete influence, final safety, or world identity.
	// Candidate::MatchesSource separately compares authored settings and region.
	bool MatchesDependencies(const FVoxelLandM5BaselineWindow& Window,
		const FVoxelLandM5CandidateResult& Candidate, const FVoxelLandM5ProtectionGrid* Protection,
		int64 WorldSeed, const FVoxelLandM5StageManifest& Manifest) const;

	// Finite, half-open LOD-zero chunk rectangle. Retains immutable input owners
	// through the final reader; callers retain provider/module lifetime separately.
	// Every chunk includes its normal stored halo and uses the generator's
	// little-endian density hash. All chunks are split from one composed field.
	// The caller must prove input provenance, complete influence, final geometry,
	// route and publication obligations. The baseline window must include the
	// additional carve-mask halo. MaxChunks is in [1, 512]. This accounts for
	// retained chunks and hash scratch, not allocator bookkeeping or upstream
	// generator/provider memory. Failure or cancellation preserves OutChunks.
	static FVoxelLandM5Diagnostic Build(const FVoxelLandM5CaveSettings& Settings,
		const FVoxelLandM5StageManifest& Manifest, int64 WorldSeed,
		TSharedRef<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Window,
		TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
		TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> Protection,
		const FIntVector& MinInclusive, const FIntVector& MaxExclusive,
		int32 MaxChunks, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe>& OutChunks);

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVoxelLandM5ScratchProtectionCheckTest;
#endif
	explicit FVoxelLandM5ScratchChunks(TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease);
	// Released after all retained input owners and output arrays.
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> SourceWindow;
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> SourceCandidate;
	TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> SourceProtection;
	int64 SourceWorldSeed = 0;
	FVoxelLandM5StageManifest SourceManifest;
	FIntVector MinInclusive = FIntVector::ZeroValue;
	FIntVector MaxExclusive = FIntVector::ZeroValue;
	int32 VoxelSizeCm = 0;
	TArray<FVoxelLandChunkData> Chunks;
};
