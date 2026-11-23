#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandChunkData.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"

struct FVoxelLandM5CaveSettings;
struct FVoxelLandM5StageManifest;
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

	// Finite, half-open LOD-zero chunk rectangle. Input storage remains immutable.
	// Every chunk includes its normal stored halo and uses the generator's
	// little-endian density hash. All chunks are split from one composed field.
	// The caller must prove input provenance, complete influence, final geometry,
	// route and publication obligations. The baseline window must include the
	// additional carve-mask halo. MaxChunks is in [1, 512]. This accounts for
	// retained chunks and hash scratch, not allocator bookkeeping or upstream
	// generator/provider memory. Failure or cancellation preserves OutChunks.
	static FVoxelLandM5Diagnostic Build(const FVoxelLandM5CaveSettings& Settings,
		const FVoxelLandM5StageManifest& Manifest, int64 WorldSeed, const FVoxelLandM5BaselineWindow& Window,
		TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
		const FVoxelLandM5ProtectionGrid* Protection, const FIntVector& MinInclusive, const FIntVector& MaxExclusive,
		int32 MaxChunks, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe>& OutChunks);

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVoxelLandM5ScratchProtectionCheckTest;
#endif
	explicit FVoxelLandM5ScratchChunks(TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease);
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	FIntVector MinInclusive = FIntVector::ZeroValue;
	FIntVector MaxExclusive = FIntVector::ZeroValue;
	int32 VoxelSizeCm = 0;
	TArray<FVoxelLandChunkData> Chunks;
};
