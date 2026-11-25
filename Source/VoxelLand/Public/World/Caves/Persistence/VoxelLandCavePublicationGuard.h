#pragma once

#include "CoreMinimal.h"
#include "World/Caves/Infrastructure/VoxelLandCaveRequestLease.h"

class FVoxelLandM5PublicationState;
struct FVoxelLandM5CavePlan;

// One guard belongs to one world. It does not schedule, cancel or join tasks,
// and does not validate a plan or own process-wide services.
class VOXELLAND_API FVoxelLandM5PublicationGuard final
{
public:
	using FLease = TSharedPtr<FVoxelLandM5RequestLease, ESPMode::ThreadSafe>;
	using FPlan = TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe>;

	// Nonpositive limits disable admissions. Key bytes must be nonempty and at
	// most MaxKeyBytes; callers supply a complete canonical dependency key.
	FVoxelLandM5PublicationGuard(int32 MaxInFlight, int32 MaxKeyBytes);
	~FVoxelLandM5PublicationGuard();
	FVoxelLandM5PublicationGuard(const FVoxelLandM5PublicationGuard&) = delete;
	FVoxelLandM5PublicationGuard& operator=(const FVoxelLandM5PublicationGuard&) = delete;

	// Failure preserves OutLease. Invalid input or exhausted admission also
	// preserves the current request/publication. Epoch exhaustion instead closes
	// the guard and clears its publication so an old token cannot become current.
	bool TryBegin(TConstArrayView<uint8> Key, FLease& OutLease);
	// Caller must have completed all required validation and must not retain a
	// mutable alias to Plan. Publication never validates geometry or topology.
	bool TryPublish(const FLease& Lease, TConstArrayView<uint8> Key, const FPlan& Plan);
	// Cancel invalidates the request, but admissions remain charged until the
	// final worker-held lease is released. Close permanently prevents admissions.
	void Cancel();
	void Close();
	FPlan GetPublished() const;
	int32 GetInFlight() const;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVoxelLandM5PublicationGuardEpochTest;
	void SetEpochForTesting(uint64 Epoch);
#endif
	TSharedRef<FVoxelLandM5PublicationState, ESPMode::ThreadSafe> State;
};
