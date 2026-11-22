#pragma once

#include "CoreMinimal.h"
#include "World/VoxelLandM4PrepassSubscription.h"

class FVoxelLandM4GridLease;
class FVoxelLandM4HeightGrid;
class FM4PrepassCoordinatorState;

// Opt-in coalescer for independently validated, exact finite-prepass identities.
// Callers must validate the complete current programs, source/payloads, providers,
// settings and algorithm versions BEFORE subscribing. Compare full canonical key
// bytes, never hashes. Only immutable UObject-free captures may enter Produce.
// Reserve executes synchronously under the coordinator lock and must not reenter
// the coordinator; it must enforce the process budget and pin the code modules.
// Produce runs on a dedicated non-game thread and must create and destroy all
// thread-affine workers there. It must create its grid with the supplied lease.
// Shutdown cancels and joins producers; call from the owner thread, not Produce.
class VOXELLAND_API FM4PrepassCoordinator final
{
public:
	using FLease = TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>;
	using FGrid = TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>;
	using FProducer = TUniqueFunction<bool(const FLease&, TFunctionRef<bool()>, FGrid&, FString&)>;

	explicit FM4PrepassCoordinator(int32 MaxActiveProducers = 2, int32 MaxRetainedResults = 2);
	~FM4PrepassCoordinator();
	FM4PrepassCoordinator(const FM4PrepassCoordinator&) = delete;
	FM4PrepassCoordinator& operator=(const FM4PrepassCoordinator&) = delete;

	// ExactKey must be nonempty and at most 8 MiB. Matching active or retained
	// results are joined before Reserve is invoked. OutError is filled on failure.
	TSharedPtr<FM4PrepassSubscription, ESPMode::ThreadSafe> Subscribe(
		TConstArrayView<uint8> ExactKey, TFunctionRef<FLease()> Reserve, FProducer&& Produce, FString& OutError);
	// On external admission pressure, release idle cache references and reap
	// completed producer tasks. Active subscriptions and grid readers retain
	// their own results and reservations. Never call from Reserve or Produce.
	void ReleaseIdleResults();
	void Shutdown();

private:
	TSharedRef<FM4PrepassCoordinatorState, ESPMode::ThreadSafe> State;
};
