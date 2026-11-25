#pragma once

#include "CoreMinimal.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"

// The returned view remains valid while this immutable result is retained.
// Keep the result, not just the view, for the complete use of the IDs.
class VOXELLANDALGO_API FVoxelLandM5SpatialQueryResult final
{
public:
	FVoxelLandM5SpatialQueryResult(const FVoxelLandM5SpatialQueryResult&) = delete;
	FVoxelLandM5SpatialQueryResult& operator=(const FVoxelLandM5SpatialQueryResult&) = delete;
	TConstArrayView<uint64> GetIds() const { return Ids; }

private:
	friend class FVoxelLandM5SpatialIndex;
	FVoxelLandM5SpatialQueryResult(
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease, TArray<uint64>&& InIds)
		: Lease(MoveTemp(InLease))
		, Ids(MoveTemp(InIds))
	{
	}
	// Declared before the array so it outlives its allocation.
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TArray<uint64> Ids;
};
