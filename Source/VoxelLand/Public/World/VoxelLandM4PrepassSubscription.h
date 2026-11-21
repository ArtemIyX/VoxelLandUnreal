#pragma once

#include "CoreMinimal.h"

class FVoxelLandM4HeightGrid;
class FM4PrepassCoordinatorState;
class FM4PrepassCoordinatorEntry;

// One consumer's interest in a shared prepass. Cancelling or releasing one
// subscription never waits for the producer and only requests producer
// cancellation when the last interested consumer detaches.
class VOXELLAND_API FM4PrepassSubscription final
{
public:
	~FM4PrepassSubscription();
	FM4PrepassSubscription(const FM4PrepassSubscription&) = delete;
	FM4PrepassSubscription& operator=(const FM4PrepassSubscription&) = delete;
	void Cancel();
	bool IsFinished() const;
	// Returns false for pending, cancelled, or failed work. OutGrid is unchanged
	// on failure and OutError describes the state. Retaining OutGrid retains the lease.
	bool ReadResult(TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid, FString& OutError) const;

private:
	friend class FM4PrepassCoordinator;
	FM4PrepassSubscription(TSharedRef<FM4PrepassCoordinatorState, ESPMode::ThreadSafe> InState,
		TSharedRef<FM4PrepassCoordinatorEntry, ESPMode::ThreadSafe> InEntry);
	TSharedRef<FM4PrepassCoordinatorState, ESPMode::ThreadSafe> State;
	TSharedPtr<FM4PrepassCoordinatorEntry, ESPMode::ThreadSafe> Entry;
	bool bAttached = true; // protected by State's lock
};
