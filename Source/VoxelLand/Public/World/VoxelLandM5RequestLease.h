#pragma once

#include "CoreMinimal.h"

class FVoxelLandM5PublicationGuard;
class FVoxelLandM5PublicationState;

// Keep a shared reference in the producer until its callable and scratch have
// actually finished. Releasing a consumer does not release the producer's slot.
class VOXELLAND_API FVoxelLandM5RequestLease final
{
public:
	~FVoxelLandM5RequestLease();
	FVoxelLandM5RequestLease(const FVoxelLandM5RequestLease&) = delete;
	FVoxelLandM5RequestLease& operator=(const FVoxelLandM5RequestLease&) = delete;

private:
	friend class FVoxelLandM5PublicationGuard;
	FVoxelLandM5RequestLease(
		TSharedRef<FVoxelLandM5PublicationState, ESPMode::ThreadSafe> InState, uint64 InEpoch, TArray<uint8>&& InKey);

	TSharedRef<FVoxelLandM5PublicationState, ESPMode::ThreadSafe> State;
	const uint64 Epoch;
	const TArray<uint8> Key;
	bool bPublished = false; // Protected by State's mutex.
};
