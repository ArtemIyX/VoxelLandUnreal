#include "Generation/VoxelLandM4HeightGrid.h"

#include "Generation/VoxelLandM4Control.h"
#include <limits>

bool FVoxelLandM4HeightGrid::Create(int64 InOriginX, int64 InOriginY, int32 InCellsX, int32 InCellsY, int64 InSpacingCm,
	TConstArrayView<int32> HeightsQ8, TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid)
{
	return Create(InOriginX, InOriginY, InCellsX, InCellsY, InSpacingCm, HeightsQ8, OutGrid, [] { return false; });
}

bool FVoxelLandM4HeightGrid::Create(int64 InOriginX, int64 InOriginY, int32 InCellsX, int32 InCellsY, int64 InSpacingCm,
	TConstArrayView<int32> HeightsQ8, TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid,
	TFunctionRef<bool()> IsCancelled)
{
	return Create(InOriginX, InOriginY, InCellsX, InCellsY, InSpacingCm, HeightsQ8, OutGrid, IsCancelled, nullptr);
}

bool FVoxelLandM4HeightGrid::Create(int64 InOriginX, int64 InOriginY, int32 InCellsX, int32 InCellsY, int64 InSpacingCm,
	TConstArrayView<int32> HeightsQ8, TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Lease)
{
	if (IsCancelled() || InCellsX < 1 || InCellsY < 1 || InCellsX > 1024 || InCellsY > 1024 || InSpacingCm <= 0)
	{
		return false;
	}
	const int64 Count = (int64(InCellsX) + 1) * (int64(InCellsY) + 1);
	if (Count > 1050625 || HeightsQ8.Num() != Count)
	{
		return false;
	}
	// Delegate the exact-intermediate bound to the numerical primitive used for
	// every sample. This check also excludes square/multiplication overflow.
	int32 Dummy = 0;
	if (!VoxelLandM4Control::BilinearHeightQ8(0, 0, 0, 0, 0, 0, InSpacingCm, Dummy)
		|| InSpacingCm > std::numeric_limits<int64>::max() / InCellsX
		|| InSpacingCm > std::numeric_limits<int64>::max() / InCellsY)
	{
		return false;
	}
	const int64 ExtentX = InSpacingCm * InCellsX;
	const int64 ExtentY = InSpacingCm * InCellsY;
	if (InOriginX > std::numeric_limits<int64>::max() - ExtentX
		|| InOriginY > std::numeric_limits<int64>::max() - ExtentY)
	{
		return false;
	}
	const int32 Width = InCellsX + 1;
	int32 MinimumHeight = MAX_int32;
	int32 MaximumHeight = MIN_int32;
	for (int32 Y = 0; Y <= InCellsY; ++Y)
	{
		if (IsCancelled())
		{
			return false;
		}
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Height = HeightsQ8[Y * Width + X];
			if (Height < -512000000 || Height > 512000000)
			{
				return false;
			}
			MinimumHeight = FMath::Min(MinimumHeight, Height);
			MaximumHeight = FMath::Max(MaximumHeight, Height);
		}
	}
	TSharedPtr<FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid = MakeShareable(new FVoxelLandM4HeightGrid());
	Grid->OriginX = InOriginX;
	Grid->OriginY = InOriginY;
	Grid->EndX = InOriginX + ExtentX;
	Grid->EndY = InOriginY + ExtentY;
	Grid->SpacingCm = InSpacingCm;
	Grid->CellsX = InCellsX;
	Grid->CellsY = InCellsY;
	Grid->MinimumHeightQ8 = MinimumHeight;
	Grid->MaximumHeightQ8 = MaximumHeight;
	Grid->Heights.Reserve(int32(Count));
	for (int32 Y = 0; Y <= InCellsY; ++Y)
	{
		if (IsCancelled())
		{
			return false;
		}
		Grid->Heights.Append(HeightsQ8.GetData() + Y * Width, Width);
	}
	if (IsCancelled())
	{
		return false;
	}
	Grid->GridLease = MoveTemp(Lease);
	OutGrid = MoveTemp(Grid);
	return true;
}

bool FVoxelLandM4HeightGrid::Sample(int64 WorldX, int64 WorldY, int32& OutHeightQ8) const
{
	if (WorldX < OriginX || WorldX > EndX || WorldY < OriginY || WorldY > EndY)
	{
		return false;
	}
	int64 X = 0, Y = 0, Rx = 0, Ry = 0;
	if (!VoxelLandM4Control::GridCoordinate(WorldX, OriginX, SpacingCm, X, Rx)
		|| !VoxelLandM4Control::GridCoordinate(WorldY, OriginY, SpacingCm, Y, Ry))
	{
		return false;
	}
	// The inclusive top/right samples are the final endpoint of the last cell.
	if (X == CellsX)
	{
		--X;
		Rx = SpacingCm;
	}
	if (Y == CellsY)
	{
		--Y;
		Ry = SpacingCm;
	}
	if (X < 0 || X >= CellsX || Y < 0 || Y >= CellsY)
	{
		return false;
	}
	const int32 Width = CellsX + 1;
	const int32 Index = int32(Y) * Width + int32(X);
	return VoxelLandM4Control::BilinearHeightQ8(Heights[Index], Heights[Index + 1], Heights[Index + Width],
		Heights[Index + Width + 1], Rx, Ry, SpacingCm, OutHeightQ8);
}
