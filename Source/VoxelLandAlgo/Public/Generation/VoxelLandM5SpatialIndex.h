#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandError.h"
#include "Data/Structs/VoxelLandM5InfluenceBounds.h"

// Immutable exhaustive index. Bounds and queries are inclusive, so face,
// edge, point, and zero-volume contacts are all returned. The caller is
// responsible for supplying bounds that encompass every density influence,
// beyond physical radius, including smoothing, displacement, filter and halo
// effects. This index cannot detect an under-inflated bound.
class VOXELLANDALGO_API FVoxelLandM5SpatialIndex final
{
public:
	// Limits pre-allocation duplicate checks and sorting, independent of input.
	static constexpr int32 MaximumEntries = 8192;

	// On failure or cancellation OutIndex retains its prior value.
	static FVoxelLandError Build(TConstArrayView<FVoxelLandM5InfluenceBounds> Entries, int32 MaxEntries,
		TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5SpatialIndex, ESPMode::ThreadSafe>& OutIndex);

	// Returns matching IDs in ascending order. On failure or cancellation OutIds
	// retains its prior value. Concurrent queries are safe with separate outputs.
	FVoxelLandError Query(const FBox3d& Bounds, TFunctionRef<bool()> IsCancelled, TArray<uint64>& OutIds) const;

private:
	FVoxelLandM5SpatialIndex() = default;
	TArray<FVoxelLandM5InfluenceBounds> EntriesById;
};
