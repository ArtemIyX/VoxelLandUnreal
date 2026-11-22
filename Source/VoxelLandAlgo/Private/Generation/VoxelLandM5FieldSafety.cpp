#include "Generation/VoxelLandM5FieldSafety.h"

#include "Generation/VoxelLandM5CheckedMath.h"

namespace
{
FVoxelLandM5Diagnostic M5FieldSafetyError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Result;
	Result.Code = Code;
	Result.Stage = FName(TEXT("FieldSafety"));
	Result.Message = Message;
	return Result;
}
}

FVoxelLandM5Diagnostic VoxelLand::M5::FieldSafety::CheckUnchangedProtectedCells(FIntVector CellCounts,
	TConstArrayView<int16> Baseline, TConstArrayView<int16> Final, TConstArrayView<uint8> ProtectedCells,
	TConstArrayView<uint8> RoofCells, TConstArrayView<uint8> EntranceCells, int64 MaxComparisons,
	TFunctionRef<bool()> IsCancelled)
{
	using namespace VoxelLand::M5::CheckedMath;
	if (CellCounts.X <= 0 || CellCounts.Y <= 0 || CellCounts.Z <= 0 || MaxComparisons < 0)
	{
		return M5FieldSafetyError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid field safety dimensions or budget"));
	}
	const int64 X = CellCounts.X;
	const int64 Y = CellCounts.Y;
	const int64 Z = CellCounts.Z;
	const int64 SX = X + 3;
	const int64 SY = Y + 3;
	const int64 SZ = Z + 3;
	int64 XY = 0;
	int64 Cells = 0;
	int64 SampleXY = 0;
	int64 Samples = 0;
	int64 RequiredWork = 0;
	if (!TryMultiplyNonNegative(X, Y, XY) || !TryMultiplyNonNegative(XY, Z, Cells)
		|| !TryMultiplyNonNegative(SX, SY, SampleXY) || !TryMultiplyNonNegative(SampleXY, SZ, Samples)
		|| !TryMultiplyNonNegative(Cells, 64, RequiredWork))
	{
		return M5FieldSafetyError(EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Field safety grid size overflow"));
	}
	if (Cells > MAX_int32 || Samples > MAX_int32 || Baseline.Num() != Samples || Final.Num() != Samples
		|| ProtectedCells.Num() != Cells || RoofCells.Num() != Cells || EntranceCells.Num() != Cells)
	{
		return M5FieldSafetyError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Field safety grid or halo size mismatch"));
	}
	if (RequiredWork > MaxComparisons)
	{
		return M5FieldSafetyError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Field safety comparison budget exceeded"));
	}
	for (int64 Cell = 0; Cell < Cells; ++Cell)
	{
		if ((Cell & 63) == 0 && IsCancelled())
		{
			return M5FieldSafetyError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Field safety check cancelled"));
		}
		if (ProtectedCells[Cell] > 1 || RoofCells[Cell] > 1 || EntranceCells[Cell] > 1)
		{
			return M5FieldSafetyError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Field safety mask is not binary"));
		}
		if (!ProtectedCells[Cell] && (!RoofCells[Cell] || EntranceCells[Cell]))
		{
			continue;
		}
		const int64 CX = Cell % X;
		const int64 CY = (Cell / X) % Y;
		const int64 CZ = Cell / XY;
		for (int64 DZ = 0; DZ < 4; ++DZ)
		{
			for (int64 DY = 0; DY < 4; ++DY)
			{
				for (int64 DX = 0; DX < 4; ++DX)
				{
					const int64 Sample = (CX + DX) + SX * ((CY + DY) + SY * (CZ + DZ));
					if (Baseline[Sample] != Final[Sample])
					{
						return M5FieldSafetyError(EVoxelLandM5ErrorCode::Blocked,
							TEXT("A protected or roof cell has a changed contributing sample"));
					}
				}
			}
		}
	}
	if (IsCancelled())
	{
		return M5FieldSafetyError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Field safety check cancelled"));
	}
	return {};
}
