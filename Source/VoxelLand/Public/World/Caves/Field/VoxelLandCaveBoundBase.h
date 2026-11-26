#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Data/Caves/Persistence/VoxelLandCaveIdentityRecord.h"
#include "Data/Caves/Planning/VoxelLandCaveWorldSnapshot.h"
#include "HAL/CriticalSection.h"
#include "Templates/Function.h"

class UVoxelLandWorldConfig;
class UVoxelLandFiniteWorldSource;
class FVoxelLandGenerator;
class FVoxelLandM4GridLease;
class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;
class FVoxelLandM5BoundBase;
class FVoxelLandM5ProtectionGrid;
class FVoxelLandM5ValidatedDefinition;
class FVoxelLandM5BoundBaseline;
class FVoxelLandWorkerContext;
class FVoxelLandOperatorRegistrySnapshot;

class FVoxelLandM5BaseCapture;

// Source constructed only by a real admitted typed capture. It owns the
// validation permit for the whole source/definition lifetime and a one-shot
// process permit until the bound finite grid claims it.
class VOXELLAND_API FVoxelLandM5FiniteNativeSource final
{
public:
	~FVoxelLandM5FiniteNativeSource();
	FVoxelLandM5FiniteNativeSource(const FVoxelLandM5FiniteNativeSource&) = delete;
	FVoxelLandM5FiniteNativeSource& operator=(const FVoxelLandM5FiniteNativeSource&) = delete;

private:
	friend class FVoxelLandM5BoundBase;
	FVoxelLandM5FiniteNativeSource(TSharedRef<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> InCapture,
		uint32 InGeometryVersion, TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease);
	TSharedRef<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Capture;
	uint32 GeometryVersion;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
};

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
	static FVoxelLandM5Diagnostic Capture(UVoxelLandWorldConfig& Config,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe>& OutCapture);
	// Typed finite equivalent. The captured bounds are authored upstream bounds,
	// not proof of a usable footprint; the caller checks freshness and lifecycle.
	static FVoxelLandM5Diagnostic Capture(UVoxelLandFiniteWorldSource& Definition,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TSharedPtr<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe>& OutCapture);
	const FVoxelLandM5WorldSnapshot& GetSnapshot() const { return Snapshot; }
	// Descriptor 3 metered identity can bind this immutable grid. Publication
	// still requires source freshness and final-field safety checks.
	TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> GetProtectionGrid() const
	{
		return ProtectionGrid;
	}
	// Game thread only. Compares protect-all, the effective mask requirement and
	// mask contents, not the rest of the world or any publication certificate.
	// Charges this capture's shared work budget before scanning. A changed dependency returns
	// success with false; invalid input or exhausted budget leaves output alone.
	FVoxelLandM5Diagnostic CheckFiniteProtectionCurrent(
		const UVoxelLandFiniteWorldSource& Definition, bool& bOutCurrent) const;

private:
	friend class FVoxelLandM5BoundBase;
	friend class FVoxelLandM5CapturedFiniteScratch;
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVoxelLandM5BoundBaseTest;
	friend class FVoxelLandM5TypedFiniteBaseCaptureTest;
#endif
	FVoxelLandM5BaseCapture(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> InBudget,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InMetadataLease,
		TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>&& InGridLease,
		TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>&& InValidationLease,
		FVoxelLandM5WorldSnapshot&& InSnapshot,
		TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe>&& InProtectionGrid);
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> ClaimGridLease() const;

	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> MetadataLease;
	mutable FCriticalSection GridLeaseMutex;
	mutable TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> GridLease;
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> ValidationLease;
	FVoxelLandM5WorldSnapshot Snapshot;
	TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> ProtectionGrid;
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

	// Game-thread typed capture, with actual process and validation admission
	// acquired before snapshot capture. Geometry is explicitly identity-bound.
	static FVoxelLandM5Diagnostic CaptureFiniteNative(UVoxelLandFiniteWorldSource& Asset,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, uint32 GeometryVersion,
		TSharedPtr<const FVoxelLandM5FiniteNativeSource, ESPMode::ThreadSafe>& OutSource);

	// Worker thread. The definition retains the exact admitted source. The dry
	// declaration and bounds have the same native semantics as DefinitionValidator;
	// they are not runtime water observations or publication authority.
	static FVoxelLandM5Diagnostic ValidateFiniteNative(
		TSharedRef<const FVoxelLandM5FiniteNativeSource, ESPMode::ThreadSafe> Source,
		TSharedRef<const FVoxelLandOperatorRegistrySnapshot, ESPMode::ThreadSafe> Registry,
		const FVoxelLandM5Bounds& EffectiveBounds, bool bExplicitBounds, bool bAuthoritativeDryWorld,
		TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe>& OutDefinition);

	// Synchronous worker only. The callback admits worker/provider resources
	// separately. A valid call claims the source process permit once, including
	// on later failure; preclaim cancellation and wrong-thread calls preserve it.
	// The definition and source must be the exact bound owners. No publication granted.
	static FVoxelLandM5Diagnostic BuildFinite(
		TSharedRef<const FVoxelLandM5FiniteNativeSource, ESPMode::ThreadSafe> Source,
		TSharedRef<const FVoxelLandM5ValidatedDefinition, ESPMode::ThreadSafe> Definition,
		TFunctionRef<FVoxelLandM5Diagnostic(const FVoxelLandGenerator&,
			TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, TFunctionRef<bool()>,
			TUniquePtr<FVoxelLandWorkerContext>&)>
			CreateWorker,
		TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5BoundBaseline, ESPMode::ThreadSafe>& OutBaseline);

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
