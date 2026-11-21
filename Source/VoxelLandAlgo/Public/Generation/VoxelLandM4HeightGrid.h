#pragma once

#include "CoreMinimal.h"

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

	/** Samples integer world centimeters inside the inclusive grid bounds.
	 * The four-corner bilinear expression is rounded once, ties away from zero.
	 * Failure, including a query outside the bounds, leaves OutHeightQ8 unchanged.
	 */
	bool Sample(int64 WorldX, int64 WorldY, int32& OutHeightQ8) const;

private:
	FVoxelLandM4HeightGrid() = default;
	int64 OriginX = 0;
	int64 OriginY = 0;
	int64 EndX = 0;
	int64 EndY = 0;
	int64 SpacingCm = 0;
	int32 CellsX = 0;
	int32 CellsY = 0;
	TArray<int32> Heights;
};
