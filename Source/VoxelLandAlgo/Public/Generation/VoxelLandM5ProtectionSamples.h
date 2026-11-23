#pragma once

#include "CoreMinimal.h"

class FVoxelLandM5BudgetLease;
class FVoxelLandM5ProtectionGrid;

// Immutable X-fastest vertex samples. The lease follows all readers.
class VOXELLANDALGO_API FVoxelLandM5ProtectionSamples final
{
public:
	FVoxelLandM5ProtectionSamples(const FVoxelLandM5ProtectionSamples&) = delete;
	FVoxelLandM5ProtectionSamples& operator=(const FVoxelLandM5ProtectionSamples&) = delete;
	TConstArrayView<uint8> GetSamples() const { return Samples; }

private:
	friend class FVoxelLandM5ProtectionGrid;
	explicit FVoxelLandM5ProtectionSamples(TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease);
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TArray<uint8> Samples;
};
