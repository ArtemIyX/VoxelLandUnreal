#include "Generation/Terrain/Erosion/VoxelLandErosion.h"

class FVoxelLandM4ErosionInternals final
{
public:
	static constexpr int64 M4ErosionMaxHeight = 512000000;
	static constexpr int32 M4ErosionMaxCells = 1024;
	static constexpr int64 M4ErosionMaxSamples = 1050625;
	static FVoxelLandError M4ErosionFail(EVoxelLandErrorCode Code, const TCHAR* Message);
	static bool M4ErosionInRange(int64 Value);
	static int32 M4ErosionLowestNeighbor(const TArray<int32>& Heights, int32 Width, int32 Height, int32 X, int32 Y);
	static bool M4ErosionAddHeight(TArray<int32>& Heights, int32 Index, int64 Delta);
};

FVoxelLandError FVoxelLandM4ErosionInternals::M4ErosionFail(EVoxelLandErrorCode Code, const TCHAR* Message)
{
	FVoxelLandError Error;
	Error.Code = Code;
	Error.Message = Message;
	return Error;
}

bool FVoxelLandM4ErosionInternals::M4ErosionInRange(int64 Value)
{
	return Value >= -M4ErosionMaxHeight && Value <= M4ErosionMaxHeight;
}

// All inputs are checked against the M4 limits before processing. With at most
// 1,050,625 Q8 samples, height sums and the sum of four neighbor transfers fit
// in int64. Hydraulic sediment is additionally bounded by capacity and slope.
int32 FVoxelLandM4ErosionInternals::M4ErosionLowestNeighbor(
	const TArray<int32>& Heights, int32 Width, int32 Height, int32 X, int32 Y)
{
	const int32 Current = Y * Width + X;
	int32 Lowest = Current;
	const int32 Dx[] = {1, -1, 0, 0};
	const int32 Dy[] = {0, 0, 1, -1};
	for (int32 Direction = 0; Direction < 4; ++Direction)
	{
		const int32 Nx = X + Dx[Direction];
		const int32 Ny = Y + Dy[Direction];
		if (Nx >= 0 && Nx < Width && Ny >= 0 && Ny < Height)
		{
			const int32 Candidate = Ny * Width + Nx;
			if (Heights[Candidate] < Heights[Lowest])
			{
				Lowest = Candidate;
			}
		}
	}
	return Lowest;
}

bool FVoxelLandM4ErosionInternals::M4ErosionAddHeight(TArray<int32>& Heights, int32 Index, int64 Delta)
{
	const int64 Result = int64(Heights[Index]) + Delta;
	if (!M4ErosionInRange(Result))
	{
		return false;
	}
	Heights[Index] = int32(Result);
	return true;
}

FVoxelLandError VoxelLandM4Erosion::Apply(int32 CellsX, int32 CellsY, TConstArrayView<int32> Heights,
	TConstArrayView<FIntPoint> DropletStarts, const FVoxelLandM4ErosionSettings& Settings, TArray<int32>& OutHeights,
	TFunctionRef<bool()> IsCancelled)
{
	if (CellsX < 1 || CellsY < 1 || CellsX > FVoxelLandM4ErosionInternals::M4ErosionMaxCells
		|| CellsY > FVoxelLandM4ErosionInternals::M4ErosionMaxCells || Settings.MaxSteps < 0 || Settings.MaxSteps > 128
		|| Settings.ThermalIterations < 0 || Settings.ThermalIterations > 32 || Settings.CapacityMaxQ8 < 0
		|| Settings.CapacityMaxQ8 > FVoxelLandM4ErosionInternals::M4ErosionMaxHeight || Settings.TalusQ8 < 0
		|| Settings.TalusQ8 > FVoxelLandM4ErosionInternals::M4ErosionMaxHeight || Settings.MaxTransferQ8 < 0
		|| Settings.MaxTransferQ8 > FVoxelLandM4ErosionInternals::M4ErosionMaxHeight || DropletStarts.Num() > 65536)
	{
		return FVoxelLandM4ErosionInternals::M4ErosionFail(
			EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid M4 erosion dimensions or settings"));
	}
	const int32 Width = CellsX + 1;
	const int32 Height = CellsY + 1;
	const int64 Count = int64(Width) * Height;
	if (Count > FVoxelLandM4ErosionInternals::M4ErosionMaxSamples || Heights.Num() != Count)
	{
		return FVoxelLandM4ErosionInternals::M4ErosionFail(
			EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid M4 erosion sample count"));
	}
	for (int32 Y = 0; Y < Height; ++Y)
	{
		if (IsCancelled())
		{
			return FVoxelLandM4ErosionInternals::M4ErosionFail(
				EVoxelLandErrorCode::Cancelled, TEXT("M4 erosion cancelled"));
		}
		for (int32 X = 0; X < Width; ++X)
		{
			if (!FVoxelLandM4ErosionInternals::M4ErosionInRange(Heights[Y * Width + X]))
			{
				return FVoxelLandM4ErosionInternals::M4ErosionFail(
					EVoxelLandErrorCode::InvalidParameter, TEXT("M4 height outside Q8 range"));
			}
		}
	}
	for (const FIntPoint& Start : DropletStarts)
	{
		if (IsCancelled())
		{
			return FVoxelLandM4ErosionInternals::M4ErosionFail(
				EVoxelLandErrorCode::Cancelled, TEXT("M4 erosion cancelled"));
		}
		if (Start.X < 0 || Start.X > CellsX || Start.Y < 0 || Start.Y > CellsY)
		{
			return FVoxelLandM4ErosionInternals::M4ErosionFail(
				EVoxelLandErrorCode::InvalidParameter, TEXT("M4 droplet start outside grid"));
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandM4ErosionInternals::M4ErosionFail(
			EVoxelLandErrorCode::Cancelled, TEXT("M4 erosion cancelled"));
	}
	TArray<int32> Working;
	Working.Append(Heights.GetData(), Heights.Num());
	for (const FIntPoint& Start : DropletStarts)
	{
		if (IsCancelled())
		{
			return FVoxelLandM4ErosionInternals::M4ErosionFail(
				EVoxelLandErrorCode::Cancelled, TEXT("M4 erosion cancelled"));
		}
		int32 Current = Start.Y * Width + Start.X;
		int64 Sediment = 0;
		for (int32 Step = 0; Step < Settings.MaxSteps; ++Step)
		{
			if (IsCancelled())
			{
				return FVoxelLandM4ErosionInternals::M4ErosionFail(
					EVoxelLandErrorCode::Cancelled, TEXT("M4 erosion cancelled"));
			}
			const int32 Next = FVoxelLandM4ErosionInternals::M4ErosionLowestNeighbor(
				Working, Width, Height, Current % Width, Current / Width);
			if (Next == Current)
			{
				break;
			}
			const int64 Slope = int64(Working[Current]) - Working[Next];
			const int64 Capacity = FMath::Min(Settings.CapacityMaxQ8, Slope / 4);
			if (Sediment > Capacity)
			{
				const int64 Deposit = (Sediment - Capacity) / 2;
				if (!FVoxelLandM4ErosionInternals::M4ErosionAddHeight(Working, Current, Deposit))
				{
					return FVoxelLandM4ErosionInternals::M4ErosionFail(
						EVoxelLandErrorCode::InvalidMathDomain, TEXT("M4 hydraulic deposit exceeds height range"));
				}
				Sediment -= Deposit;
			}
			else
			{
				const int64 Removed =
					FMath::Min(FMath::Min((Capacity - Sediment) / 2, Settings.MaxTransferQ8), Slope / 4);
				if (!FVoxelLandM4ErosionInternals::M4ErosionAddHeight(Working, Current, -Removed))
				{
					return FVoxelLandM4ErosionInternals::M4ErosionFail(
						EVoxelLandErrorCode::InvalidMathDomain, TEXT("M4 hydraulic erosion exceeds height range"));
				}
				Sediment += Removed;
			}
			Current = Next;
		}
		if (!FVoxelLandM4ErosionInternals::M4ErosionAddHeight(Working, Current, Sediment))
		{
			return FVoxelLandM4ErosionInternals::M4ErosionFail(
				EVoxelLandErrorCode::InvalidMathDomain, TEXT("M4 terminal deposit exceeds height range"));
		}
	}
	TArray<int64> Delta;
	if (Settings.ThermalIterations > 0)
	{
		Delta.SetNumZeroed(int32(Count));
	}
	for (int32 Iteration = 0; Iteration < Settings.ThermalIterations; ++Iteration)
	{
		FMemory::Memzero(Delta.GetData(), Delta.Num() * sizeof(int64));
		for (int32 Y = 0; Y < Height; ++Y)
		{
			if (IsCancelled())
			{
				return FVoxelLandM4ErosionInternals::M4ErosionFail(
					EVoxelLandErrorCode::Cancelled, TEXT("M4 erosion cancelled"));
			}
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 Current = Y * Width + X;
				const int32 Next = FVoxelLandM4ErosionInternals::M4ErosionLowestNeighbor(Working, Width, Height, X, Y);
				const int64 Difference = int64(Working[Current]) - Working[Next];
				if (Difference > Settings.TalusQ8)
				{
					const int64 Transfer = FMath::Min(Settings.MaxTransferQ8, (Difference - Settings.TalusQ8) / 4);
					Delta[Current] -= Transfer;
					Delta[Next] += Transfer;
				}
			}
		}
		for (int32 Y = 0; Y < Height; ++Y)
		{
			if (IsCancelled())
			{
				return FVoxelLandM4ErosionInternals::M4ErosionFail(
					EVoxelLandErrorCode::Cancelled, TEXT("M4 erosion cancelled"));
			}
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 Index = Y * Width + X;
				if (!FVoxelLandM4ErosionInternals::M4ErosionAddHeight(Working, Index, Delta[Index]))
				{
					return FVoxelLandM4ErosionInternals::M4ErosionFail(
						EVoxelLandErrorCode::InvalidMathDomain, TEXT("M4 thermal transfer exceeds height range"));
				}
			}
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandM4ErosionInternals::M4ErosionFail(
			EVoxelLandErrorCode::Cancelled, TEXT("M4 erosion cancelled"));
	}
	OutHeights = MoveTemp(Working);
	return {};
}