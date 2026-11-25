#pragma once

#include "CoreMinimal.h"
#include "Data/Field/Chunks/VoxelLandError.h"
#include "Data/Caves/Field/VoxelLandCaveInfluenceBounds.h"

class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;
class FVoxelLandM5SpatialQueryResult;

// Immutable exhaustive index. Bounds and queries are inclusive, so face,
// edge, point, and zero-volume contacts are all returned. The caller is
// responsible for supplying bounds that encompass every density influence,
// beyond physical radius, including smoothing, displacement, filter and halo
// effects. This index cannot detect an under-inflated bound.
// The caller must pass the plan's shared budget when building this index and
// ensure all collaborating stages use that same budget. The reservations
// account for estimated index/query storage; they do not establish a total
// process-memory bound or include caller-owned inputs and allocator overhead.
class VOXELLANDALGO_API FVoxelLandM5SpatialIndex final
{
public:
	FVoxelLandM5SpatialIndex(const FVoxelLandM5SpatialIndex&) = delete;
	FVoxelLandM5SpatialIndex& operator=(const FVoxelLandM5SpatialIndex&) = delete;
	// Limits pre-allocation duplicate checks and sorting, independent of input.
	static constexpr int32 MaximumEntries = 8192;

	// On failure or cancellation OutIndex retains its prior value.
	static FVoxelLandError Build(TConstArrayView<FVoxelLandM5InfluenceBounds> Entries, int32 MaxEntries,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5SpatialIndex, ESPMode::ThreadSafe>& OutIndex);

	// Returns matching IDs in ascending order in an immutable result retaining
	// its own lease from the index budget. Failure preserves the prior result.
	// Concurrent queries are safe with separate outputs.
	FVoxelLandError Query(const FBox3d& Bounds, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5SpatialQueryResult, ESPMode::ThreadSafe>& OutResult) const;

private:
	FVoxelLandM5SpatialIndex(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> InBudget,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease);
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	// Retain admission until after the entry array has been destroyed.
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TArray<FVoxelLandM5InfluenceBounds> EntriesById;
};
