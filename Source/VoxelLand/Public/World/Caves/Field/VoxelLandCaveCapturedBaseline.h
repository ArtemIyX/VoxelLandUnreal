#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Templates/Function.h"

class FVoxelLandM5BudgetLease;
class FVoxelLandM5BaselineWindow;
class FVoxelLandM5CapturedFiniteScratch;
class FVoxelLandWorkerContext;
struct FVoxelLandM5PointCm;

// A bounded window from this captured scratch's exact generator and budget.
// It does not prove complete coverage, source freshness, final-field safety or
// cave/world certification. Caller separately admits worker/provider resources
// and per-generation work for the entire synchronous call.
class VOXELLAND_API FVoxelLandM5CapturedBaseline final
{
public:
	// Worker thread only. Failure or cancellation preserves Out.
	static FVoxelLandM5Diagnostic Build(
		TSharedRef<const FVoxelLandM5CapturedFiniteScratch, ESPMode::ThreadSafe> Captured, const FIntVector& MinChunk,
		const FIntVector& MaxChunk, int32 MaxChunks, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe>& Out);
	// Exact bounded lattice box with the captured generator and shared budget.
	// The caller admits and retains Worker and provider resources for this call.
	// Incorrect worker or cancellation preserves Out.
	static FVoxelLandM5Diagnostic BuildBox(
		TSharedRef<const FVoxelLandM5CapturedFiniteScratch, ESPMode::ThreadSafe> Captured,
		FVoxelLandWorkerContext& Worker, const FVoxelLandM5PointCm& MinLattice, const FIntVector& Size,
		TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5CapturedBaseline, ESPMode::ThreadSafe>& Out);
	const FVoxelLandM5CapturedFiniteScratch& GetCaptured() const { return *Captured; }
	const FVoxelLandM5BaselineWindow& GetWindow() const { return *Window; }
	TSharedRef<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> GetWindowShared() const
	{
		return Window.ToSharedRef();
	}

private:
	FVoxelLandM5CapturedBaseline(TSharedRef<const FVoxelLandM5CapturedFiniteScratch, ESPMode::ThreadSafe> InCaptured,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
		: Captured(MoveTemp(InCaptured))
		, Lease(MoveTemp(InLease))
	{
	}
	TSharedRef<const FVoxelLandM5CapturedFiniteScratch, ESPMode::ThreadSafe> Captured;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Window;
};
