#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Data/Structs/VoxelLandM5IdentityRecord.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "HAL/CriticalSection.h"
#include "Templates/Function.h"

class UVoxelLandM4WorldConfig;
class UVoxelLandFiniteWorld;
class FVoxelLandGenerator;
class FVoxelLandM4GridLease;
class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;
class FVoxelLandM5BoundBase;

// Immutable UObject-free game-thread capture, with real M4 admission tokens.
// It is a private input to base preparation, not a validated cave plan.
class VOXELLAND_API FVoxelLandM5BaseCapture final
{
public:
	FVoxelLandM5BaseCapture(const FVoxelLandM5BaseCapture&) = delete;
	FVoxelLandM5BaseCapture& operator=(const FVoxelLandM5BaseCapture&) = delete;

	// Game thread only. Acquires the M4 process reservation and validation
	// permit before authoring capture. Snapshot bytes are a bounded M4 capture;
	// M4's allowance is separate from the M5 meter and does not measure native
	// provider/allocator overhead. The M5 reservation covers a logical
	// metadata allowance, not the M4 prepass, worker scratch or M4 work.
	// Failure leaves OutCapture unchanged. The returned capture contains no UObject.
	static FVoxelLandM5Diagnostic Capture(UVoxelLandM4WorldConfig& Config,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe>& OutCapture);
	// Typed finite equivalent. The captured bounds are authored upstream bounds,
	// not proof of a usable footprint; the caller checks freshness and lifecycle.
	static FVoxelLandM5Diagnostic Capture(UVoxelLandFiniteWorld& Definition,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe>& OutCapture);
	const FVoxelLandM5WorldSnapshot& GetSnapshot() const { return Snapshot; }

private:
	friend class FVoxelLandM5BoundBase;
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVoxelLandM5BoundBaseTest;
	friend class FVoxelLandM5TypedFiniteBaseCaptureTest;
#endif
	FVoxelLandM5BaseCapture(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> InBudget,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InMetadataLease,
		TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>&& InGridLease,
		TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>&& InValidationLease,
		FVoxelLandM5WorldSnapshot&& InSnapshot);
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> ClaimGridLease() const;

	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> MetadataLease;
	mutable FCriticalSection GridLeaseMutex;
	mutable TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> GridLease;
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> ValidationLease;
	FVoxelLandM5WorldSnapshot Snapshot;
};

// Reserved for an eventual exact captured-descriptor/generator binding. No
// instance is currently produced: shared-budget preparation is unavailable.
// It does not certify caves, masks, final fields, routes or world publication.
class VOXELLAND_API FVoxelLandM5BoundBase final
{
public:
	FVoxelLandM5BoundBase(const FVoxelLandM5BoundBase&) = delete;
	FVoxelLandM5BoundBase& operator=(const FVoxelLandM5BoundBase&) = delete;

	// Worker thread only. This one-shot input currently fails closed with
	// Unproven: M4 identity validation and preparation do not yet debit the
	// shared M5 work/memory meter. The claimed M4 reservation is released after
	// this call; no generator or bound base is published. Cancellation or work
	// exhaustion also consumes the reservation. Wrong-thread calls do not.
	// The caller owns its asynchronous job and must cancel/drain it; source
	// freshness and complete provider/process admission remain separate work.
	static FVoxelLandM5Diagnostic Prepare(TSharedRef<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Capture,
		TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5BoundBase, ESPMode::ThreadSafe>& OutBase);

	const FVoxelLandM5WorldSnapshot& GetSnapshot() const { return Capture->Snapshot; }
	const FVoxelLandM5IdentityRecord& GetIdentity() const { return Identity; }
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> GetBudget() const { return Capture->Budget; }

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVoxelLandM5BoundBaseTest;
#endif
	FVoxelLandM5BoundBase(TSharedRef<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> InCapture,
		TSharedRef<const FVoxelLandGenerator, ESPMode::ThreadSafe> InGenerator,
		FVoxelLandM5IdentityRecord&& InIdentity);
	// Capture and its leases outlive the generator and identity.
	TSharedRef<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Capture;
	TSharedRef<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
	FVoxelLandM5IdentityRecord Identity;
};
