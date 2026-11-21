#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM4StageStats.h"
#include "Generation/VoxelLandM4GridLease.h"

/** Immutable bounded Q8 height snapshot for the M4 prepass foundation.
 * This record does not configure or enable a production world domain.
 */
class VOXELLANDALGO_API FVoxelLandM4HeightGrid final
{
public:
	/** Creates a private snapshot of X-fastest row-major samples. CellsX and
	 * CellsY are in [1,1024] and the sample count may not exceed 1,050,625.
	 * SpacingCm is a positive integer and must satisfy
	 * SpacingCm^2 * 512000000 <= INT64_MAX: this permits exact int64 bilinear
	 * interpolation even for the full [-512000000,512000000] Q8 height range.
	 * Both inclusive endpoints Origin + Cells * Spacing must fit in int64.
	 * On failure, OutGrid is left unchanged.
	 */
	static bool Create(int64 OriginX, int64 OriginY, int32 CellsX, int32 CellsY, int64 SpacingCm,
		TConstArrayView<int32> HeightsQ8, TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid);

	/** Cancellable variant. Checks IsCancelled at least once per row during
	 * validation and snapshot copying, and immediately before publication.
	 * A cancelled operation leaves OutGrid unchanged.
	 */
	static bool Create(int64 OriginX, int64 OriginY, int32 CellsX, int32 CellsY, int64 SpacingCm,
		TConstArrayView<int32> HeightsQ8, TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid,
		TFunctionRef<bool()> IsCancelled);

	// The immutable snapshot retains Lease until its final reader releases it.
	static bool Create(int64 OriginX, int64 OriginY, int32 CellsX, int32 CellsY, int64 SpacingCm,
		TConstArrayView<int32> HeightsQ8, TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid,
		TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Lease);
	// Stage statistics are copied before publication; callers must provide only
	// observations from the exact control samples that produced HeightsQ8.
	static bool Create(int64 OriginX, int64 OriginY, int32 CellsX, int32 CellsY, int64 SpacingCm,
		TConstArrayView<int32> HeightsQ8, TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid,
		TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Lease,
		const FVoxelLandM4StageStats* StageStats);

	/** Samples integer world centimeters inside the inclusive grid bounds.
	 * The four-corner bilinear expression is rounded once, ties away from zero.
	 * Failure, including a query outside the bounds, leaves OutHeightQ8 unchanged.
	 */
	bool Sample(int64 WorldX, int64 WorldY, int32& OutHeightQ8) const;

	// Inclusive sample-grid bounds in integer centimeters. Range covers the
	// immutable stored heights only; it excludes subsequent overhang and filters.
	void GetBoundsCm(int64& OutMinX, int64& OutMinY, int64& OutMaxX, int64& OutMaxY) const
	{
		OutMinX = OriginX;
		OutMinY = OriginY;
		OutMaxX = EndX;
		OutMaxY = EndY;
	}
	void GetHeightRangeQ8(int32& OutMinimum, int32& OutMaximum) const
	{
		OutMinimum = MinimumHeightQ8;
		OutMaximum = MaximumHeightQ8;
	}
	// False for grids created without the complete observed control stages.
	bool GetStageStats(FVoxelLandM4StageStats& OutStats) const
	{
		if (!StageStats.IsSet())
		{
			return false;
		}
		OutStats = StageStats.GetValue();
		return true;
	}
	// Cache producers may publish only a grid that retains their exact lease.
	bool RetainsLease(const TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>& Lease) const
	{
		return Lease.IsValid() && GridLease == Lease;
	}

private:
	FVoxelLandM4HeightGrid() = default;
	int64 OriginX = 0;
	int64 OriginY = 0;
	int64 EndX = 0;
	int64 EndY = 0;
	int64 SpacingCm = 0;
	int32 CellsX = 0;
	int32 CellsY = 0;
	int32 MinimumHeightQ8 = 0;
	int32 MaximumHeightQ8 = 0;
	TOptional<FVoxelLandM4StageStats> StageStats;
	// Declared before Heights so its destructor runs after the grid allocation is released.
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> GridLease;
	TArray<int32> Heights;
};
