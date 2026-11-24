#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"

class FVoxelLandGenerator;
class FVoxelLandWorkerContext;
class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;
class FVoxelLandM5BaseCapture;
class FVoxelLandM5MeteredIdentity;
class FVoxelLandM5FiniteBaseScratch;

// Captured input, metered descriptor and finite base scratch only. This does not
// certify world freshness, providers, caves, final fields or publication.
class VOXELLAND_API FVoxelLandM5CapturedFiniteScratch final
{
public:
	// Worker thread only. Wrong-thread calls do not claim the capture. Every
	// worker-thread attempt claims the one-shot process reservation, including
	// cancellation and failure; successful grids retain it until final release.
	// The factory synchronously creates workers on this thread for its supplied
	// generator. Caller admits and retains worker/provider resources and allocator
	// overhead. Output is unchanged on failure or cancellation.
	static FVoxelLandM5Diagnostic Build(TSharedRef<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Capture,
		TFunctionRef<FVoxelLandM5Diagnostic(const FVoxelLandGenerator&,
			TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>, TFunctionRef<bool()>,
			TUniquePtr<FVoxelLandWorkerContext>&)> CreateWorker, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5CapturedFiniteScratch, ESPMode::ThreadSafe>& Out);
	const FVoxelLandM5BaseCapture& GetCapture() const { return *Capture; }
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> GetBudget() const;
	const FVoxelLandM5MeteredIdentity& GetIdentity() const { return *Identity; }
	const FVoxelLandM5FiniteBaseScratch& GetScratch() const { return *Scratch; }
private:
	FVoxelLandM5CapturedFiniteScratch(TSharedRef<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> InCapture,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
		: Capture(MoveTemp(InCapture)), Lease(MoveTemp(InLease)) {}
	TSharedRef<const FVoxelLandM5BaseCapture, ESPMode::ThreadSafe> Capture;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TSharedPtr<const FVoxelLandM5MeteredIdentity, ESPMode::ThreadSafe> Identity;
	TSharedPtr<const FVoxelLandM5FiniteBaseScratch, ESPMode::ThreadSafe> Scratch;
};
