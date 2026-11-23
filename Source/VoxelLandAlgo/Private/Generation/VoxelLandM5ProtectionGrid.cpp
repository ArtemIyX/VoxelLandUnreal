#include "Generation/VoxelLandM5ProtectionGrid.h"

#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5ProtectionSamples.h"

class FVoxelLandM5ProtectionGridInternals final
{
public:
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = FName(TEXT("ProtectionGrid"));
		Result.Message = Message;
		return Result;
	}

	static bool Count(FIntVector Size, int64& Out)
	{
		using namespace VoxelLand::M5::CheckedMath;
		int64 XY = 0;
		return Size.X > 0 && Size.Y > 0 && Size.Z > 0 && TryMultiplyNonNegative(Size.X, Size.Y, XY)
			&& TryMultiplyNonNegative(XY, Size.Z, Out) && Out <= MAX_int32;
	}
};

FVoxelLandM5ProtectionGrid::FVoxelLandM5ProtectionGrid(FIntVector InSize,
	const FVoxelLandM5PointCm& InOriginCm, int64 InSpacingCm,
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
	: Size(InSize), OriginCm(InOriginCm), SpacingCm(InSpacingCm), Lease(MoveTemp(InLease))
{
}

FVoxelLandM5ProtectionSamples::FVoxelLandM5ProtectionSamples(
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
	: Lease(MoveTemp(InLease))
{
}

FVoxelLandM5Diagnostic FVoxelLandM5ProtectionGrid::Capture(FIntVector Size,
	const FVoxelLandM5PointCm& OriginCm, int64 SpacingCm, TConstArrayView<uint8> Cells,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe>& OutGrid)
{
	using namespace VoxelLand::M5::CheckedMath;
	int64 Count = 0;
	if (SpacingCm <= 0 || !FVoxelLandM5ProtectionGridInternals::Count(Size, Count) || Cells.Num() != Count)
	{
		return FVoxelLandM5ProtectionGridInternals::Error(EVoxelLandM5ErrorCode::InvalidInput,
			TEXT("Invalid protection cell grid or input size"));
	}
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int64 N = Axis == 0 ? Size.X : Axis == 1 ? Size.Y : Size.Z;
		const int64 Origin = Axis == 0 ? OriginCm.X : Axis == 1 ? OriginCm.Y : OriginCm.Z;
		int64 Extent = 0, End = 0;
		if (!TryMultiply(N, SpacingCm, Extent) || !TryAdd(Origin, Extent, End))
		{
			return FVoxelLandM5ProtectionGridInternals::Error(EVoxelLandM5ErrorCode::ArithmeticOverflow,
				TEXT("Protection grid extent overflow"));
		}
	}
	int64 Bytes = 0, Work = 0;
	if (!TryAdd(Count, 4096, Bytes) || !TryMultiplyNonNegative(Count, 2, Work) || !TryAdd(Work, 1, Work))
	{
		return FVoxelLandM5ProtectionGridInternals::Error(EVoxelLandM5ErrorCode::ArithmeticOverflow,
			TEXT("Protection grid admission overflow"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5ProtectionGridInternals::Error(EVoxelLandM5ErrorCode::Cancelled,
			TEXT("Protection grid capture cancelled"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(Bytes, Lease) || !Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5ProtectionGridInternals::Error(EVoxelLandM5ErrorCode::BudgetExceeded,
			TEXT("Protection grid admission exhausted"));
	}
	TSharedPtr<FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> Scratch =
		MakeShareable(new FVoxelLandM5ProtectionGrid(Size, OriginCm, SpacingCm, MoveTemp(Lease)));
	Scratch->Cells.SetNumUninitialized(int32(Count));
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if ((Index & 1023) == 0 && IsCancelled())
		{
			return FVoxelLandM5ProtectionGridInternals::Error(EVoxelLandM5ErrorCode::Cancelled,
				TEXT("Protection grid capture cancelled"));
		}
		const uint8 Value = Cells[Index];
		if (Value > 1)
		{
			return FVoxelLandM5ProtectionGridInternals::Error(EVoxelLandM5ErrorCode::InvalidInput,
				TEXT("Protection cells must be binary"));
		}
		Scratch->Cells[Index] = Value;
	}
	if (IsCancelled())
	{
		return FVoxelLandM5ProtectionGridInternals::Error(EVoxelLandM5ErrorCode::Cancelled,
			TEXT("Protection grid capture cancelled"));
	}
	OutGrid = MoveTemp(Scratch);
	return {};
}

FVoxelLandM5Diagnostic FVoxelLandM5ProtectionGrid::Project(FIntVector SampleSize,
	const FVoxelLandM5PointCm& SampleOriginCm, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5ProtectionSamples, ESPMode::ThreadSafe>& OutSamples) const
{
	using namespace VoxelLand::M5::CheckedMath;
	if (!Lease.IsValid() || !Budget->OwnsLease(*Lease))
	{
		return FVoxelLandM5ProtectionGridInternals::Error(EVoxelLandM5ErrorCode::InvalidInput,
			TEXT("Protection grid belongs to a different budget"));
	}
	int64 Count = 0;
	if (!FVoxelLandM5ProtectionGridInternals::Count(SampleSize, Count))
	{
		return FVoxelLandM5ProtectionGridInternals::Error(EVoxelLandM5ErrorCode::InvalidInput,
			TEXT("Invalid protection sample dimensions"));
	}
	int64 Start[3] = {};
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int64 N = Axis == 0 ? Size.X : Axis == 1 ? Size.Y : Size.Z;
		const int64 Samples = Axis == 0 ? SampleSize.X : Axis == 1 ? SampleSize.Y : SampleSize.Z;
		const int64 Origin = Axis == 0 ? OriginCm.X : Axis == 1 ? OriginCm.Y : OriginCm.Z;
		const int64 SampleOrigin = Axis == 0 ? SampleOriginCm.X : Axis == 1 ? SampleOriginCm.Y : SampleOriginCm.Z;
		int64 Delta = 0, End = 0;
		if (!TrySubtract(SampleOrigin, Origin, Delta))
		{
			return FVoxelLandM5ProtectionGridInternals::Error(EVoxelLandM5ErrorCode::ArithmeticOverflow,
				TEXT("Protection sample origin overflow"));
		}
		if (Delta % SpacingCm != 0)
		{
			return FVoxelLandM5ProtectionGridInternals::Error(EVoxelLandM5ErrorCode::InvalidInput,
				TEXT("Protection samples must align to the cell lattice"));
		}
		Start[Axis] = Delta / SpacingCm;
		if (!TryAdd(Start[Axis], Samples - 1, End) || Start[Axis] < 1 || End >= N)
		{
			return FVoxelLandM5ProtectionGridInternals::Error(EVoxelLandM5ErrorCode::OutOfDomain,
				TEXT("Protection grid lacks incident cell coverage"));
		}
	}
	int64 Bytes = 0, Work = 0;
	if (!TryAdd(Count, 4096, Bytes) || !TryMultiplyNonNegative(Count, 10, Work) || !TryAdd(Work, 1, Work))
	{
		return FVoxelLandM5ProtectionGridInternals::Error(EVoxelLandM5ErrorCode::ArithmeticOverflow,
			TEXT("Protection sample admission overflow"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5ProtectionGridInternals::Error(EVoxelLandM5ErrorCode::Cancelled,
			TEXT("Protection projection cancelled"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(Bytes, Lease) || !Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5ProtectionGridInternals::Error(EVoxelLandM5ErrorCode::BudgetExceeded,
			TEXT("Protection sample admission exhausted"));
	}
	TSharedPtr<FVoxelLandM5ProtectionSamples, ESPMode::ThreadSafe> Scratch =
		MakeShareable(new FVoxelLandM5ProtectionSamples(MoveTemp(Lease)));
	Scratch->Samples.SetNumUninitialized(int32(Count));
	int32 OutputIndex = 0;
	for (int64 Z = 0; Z < SampleSize.Z; ++Z)
	{
		for (int64 Y = 0; Y < SampleSize.Y; ++Y)
		{
			for (int64 X = 0; X < SampleSize.X; ++X, ++OutputIndex)
			{
				if ((OutputIndex & 1023) == 0 && IsCancelled())
				{
					return FVoxelLandM5ProtectionGridInternals::Error(EVoxelLandM5ErrorCode::Cancelled,
						TEXT("Protection projection cancelled"));
				}
				uint8 Protected = 0;
				for (int64 DZ = -1; DZ <= 0; ++DZ)
				{
					for (int64 DY = -1; DY <= 0; ++DY)
					{
						for (int64 DX = -1; DX <= 0; ++DX)
						{
							const int64 Index = (Start[0] + X + DX) + int64(Size.X)
								* ((Start[1] + Y + DY) + int64(Size.Y) * (Start[2] + Z + DZ));
							Protected |= Cells[int32(Index)];
						}
					}
				}
				Scratch->Samples[OutputIndex] = Protected;
			}
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandM5ProtectionGridInternals::Error(EVoxelLandM5ErrorCode::Cancelled,
			TEXT("Protection projection cancelled"));
	}
	OutSamples = MoveTemp(Scratch);
	return {};
}
