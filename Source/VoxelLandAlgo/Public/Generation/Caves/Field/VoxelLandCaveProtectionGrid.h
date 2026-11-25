#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Data/Caves/Geometry/VoxelLandCavePointCm.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;
class FVoxelLandM5BudgetLease;
class FVoxelLandM5ProtectionSamples;

// Immutable X-fastest binary cells; each cell owns its half-open world box.
class VOXELLANDALGO_API FVoxelLandM5ProtectionGrid final
{
public:
	FVoxelLandM5ProtectionGrid(const FVoxelLandM5ProtectionGrid&) = delete;
	FVoxelLandM5ProtectionGrid& operator=(const FVoxelLandM5ProtectionGrid&) = delete;

	// Views remain valid only while this immutable grid is retained.
	FIntVector GetSize() const { return Size; }
	const FVoxelLandM5PointCm& GetOriginCm() const { return OriginCm; }
	int64 GetSpacingCm() const { return SpacingCm; }
	TConstArrayView<uint8> GetCells() const { return Cells; }
	bool IsOwnedBy(const FVoxelLandM5Budget& Budget) const;

	// Input must remain immutable during capture. Size is a cell count, origin is
	// its minimum corner, and spacing is positive whole centimetres. A 1 marks
	// the whole cell protected. The supplied box must include every relevant
	// protected cell; this class cannot prove input provenance or completeness.
	// Output is unchanged on failure or cancellation. Input allocations and
	// allocator bookkeeping are not included in the budget.
	static FVoxelLandM5Diagnostic Capture(FIntVector Size, const FVoxelLandM5PointCm& OriginCm, int64 SpacingCm,
		TConstArrayView<uint8> Cells, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe>& OutGrid);

	// Same-spacing aligned vertices only. A vertex is marked if any of its eight
	// incident cells is protected. All eight must exist even at grid boundaries;
	// missing exterior/halo returns OutOfDomain. Result is X-fastest and can feed
	// CarveMasks::Build. This does not certify mesher/filter influence, final
	// field safety, water policy, identity, or authoritative input provenance.
	// Budget must be the same accounting object used for capture.
	// Output is unchanged on failure or cancellation.
	FVoxelLandM5Diagnostic Project(FIntVector SampleSize, const FVoxelLandM5PointCm& SampleOriginCm,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5ProtectionSamples, ESPMode::ThreadSafe>& OutSamples) const;

	// SourceBudget verifies the retained grid; JobBudget pays for projection.
	FVoxelLandM5Diagnostic ProjectForJob(FIntVector SampleSize, const FVoxelLandM5PointCm& SampleOriginCm,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> SourceBudget,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> JobBudget, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5ProtectionSamples, ESPMode::ThreadSafe>& OutSamples) const;

	// Same-spacing aligned XY vertices. A column is marked if any of its four
	// incident XY cells is protected at any captured Z. All four cells must be
	// present; missing XY halo returns OutOfDomain. Result is X-fastest for
	// SurfaceQuery::QueryFlatSurface. ColumnOriginCm.Z is ignored. The caller
	// must prove the captured Z domain and XY influence are complete, and must
	// separately certify provenance, water, surface and final-field safety.
	// Budget must be the same accounting object used for capture. Output is
	// unchanged on failure or cancellation.
	FVoxelLandM5Diagnostic ProjectColumns(FIntPoint ColumnCount, const FVoxelLandM5PointCm& ColumnOriginCm,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
		TSharedPtr<const FVoxelLandM5ProtectionSamples, ESPMode::ThreadSafe>& OutColumns) const;

private:
	FVoxelLandM5ProtectionGrid(FIntVector InSize, const FVoxelLandM5PointCm& InOriginCm, int64 InSpacingCm,
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease);
	FIntVector Size;
	FVoxelLandM5PointCm OriginCm;
	int64 SpacingCm;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TArray<uint8> Cells;
};
