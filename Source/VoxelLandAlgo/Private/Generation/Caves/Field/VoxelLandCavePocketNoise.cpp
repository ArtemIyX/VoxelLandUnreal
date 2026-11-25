#include "Generation/Caves/Field/VoxelLandCavePocketNoise.h"

#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"
#include "Generation/Caves/Planning/VoxelLandCaveSeed.h"

class FVoxelLandM5PocketNoiseInternals final
{
public:
	static constexpr int64 M5PocketLimitCm = 1048576;
	static constexpr int64 M5PocketOne = 65536;
	static void Append32(TArray<uint8>& Bytes, uint32 Value);
	static void Append64(TArray<uint8>& Bytes, uint64 Value);
	static int32 Interpolate(int32 A, int32 B, int64 Weight);
};

void FVoxelLandM5PocketNoiseInternals::Append32(TArray<uint8>& Bytes, uint32 Value)
{
	for (uint32 Shift = 0; Shift < 32; Shift += 8)
	{
		Bytes.Add(static_cast<uint8>(Value >> Shift));
	}
}

void FVoxelLandM5PocketNoiseInternals::Append64(TArray<uint8>& Bytes, uint64 Value)
{
	for (uint32 Shift = 0; Shift < 64; Shift += 8)
	{
		Bytes.Add(static_cast<uint8>(Value >> Shift));
	}
}

// Bounds guarantee a numerator in [-2^31, 2^31); floor is explicit for
// negative results because C++ signed division truncates toward zero.
int32 FVoxelLandM5PocketNoiseInternals::Interpolate(int32 A, int32 B, int64 Weight)
{
	const int64 Numerator = int64(A) * (M5PocketOne - Weight) + int64(B) * Weight;
	int64 Value = Numerator / M5PocketOne;
	if (Numerator % M5PocketOne < 0)
	{
		--Value;
	}
	return static_cast<int32>(Value);
}

namespace
{
FVoxelLandM5Diagnostic CacheError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Result;
	Result.Code = Code;
	Result.Stage = TEXT("M5.PocketNoiseCache");
	Result.Message = Message;
	return Result;
}
}

FVoxelLandM5PocketNoiseCache::FVoxelLandM5PocketNoiseCache(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> InBudget,
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
	: Budget(InBudget)
	, Lease(MoveTemp(InLease))
{
}

FVoxelLandM5PocketNoiseCache::~FVoxelLandM5PocketNoiseCache() = default;

FVoxelLandM5Diagnostic FVoxelLandM5PocketNoiseCache::Build(const FVoxelLandM5PointCm& MinCm,
	const FVoxelLandM5PointCm& MaxCmInclusive, int64 PeriodCm, int64 WorldSeed, const FGuid& StageId, uint64 Salt,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> InBudget, TFunctionRef<bool()> IsCancelled,
	TUniquePtr<FVoxelLandM5PocketNoiseCache>& Out)
{
	using namespace VoxelLand::M5::CheckedMath;
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled())
	{
		return CacheError(E::Cancelled, TEXT("Pocket cache cancelled"));
	}
	constexpr int64 Limit = FVoxelLandM5PocketNoiseInternals::M5PocketLimitCm;
	constexpr int64 MaxCorners = 1000000;
	const int64 Min[3] = {MinCm.X, MinCm.Y, MinCm.Z};
	const int64 Max[3] = {MaxCmInclusive.X, MaxCmInclusive.Y, MaxCmInclusive.Z};
	if (PeriodCm <= 0 || PeriodCm > Limit)
	{
		return CacheError(E::InvalidInput, TEXT("Invalid pocket cache period"));
	}
	int64 MinCellLocal[3], Extents[3];
	int64 Count = 1;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		int64 LastCell = 0;
		if (Min[Axis] < -Limit || Max[Axis] > Limit || Min[Axis] > Max[Axis])
		{
			return CacheError(E::OutOfDomain, TEXT("Invalid pocket cache bounds"));
		}
		if (!TryFloorDivide(Min[Axis], PeriodCm, MinCellLocal[Axis]) || !TryFloorDivide(Max[Axis], PeriodCm, LastCell)
			|| !TrySubtract(LastCell, MinCellLocal[Axis], Extents[Axis]) || !TryAdd(Extents[Axis], 2, Extents[Axis])
			|| !TryMultiplyNonNegative(Count, Extents[Axis], Count))
		{
			return CacheError(E::ArithmeticOverflow, TEXT("Pocket cache corner count overflow"));
		}
	}
	if (Count > MaxCorners)
	{
		return CacheError(E::BudgetExceeded, TEXT("Pocket cache corner limit exceeded"));
	}
	int64 Bytes = 0, Work = 0;
	// Two arrays, allocator slack, packet/digest scratch, object and control overhead.
	if (!TryMultiplyNonNegative(Count, 2 * int64(sizeof(int32) + sizeof(uint8)), Bytes) || !TryAdd(Bytes, 12288, Bytes)
		|| !TryMultiplyNonNegative(Count, 2, Work) || !TryAdd(Work, 64, Work))
	{
		return CacheError(E::ArithmeticOverflow, TEXT("Pocket cache admission overflow"));
	}
	if (!InBudget->TryChargeWork(Work))
	{
		return CacheError(E::BudgetExceeded, TEXT("Pocket cache initialization work exhausted"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> NewLease;
	if (!InBudget->TryReserveBytes(Bytes, NewLease))
	{
		return CacheError(E::BudgetExceeded, TEXT("Pocket cache memory exhausted"));
	}
	TUniquePtr<FVoxelLandM5PocketNoiseCache> Result(new FVoxelLandM5PocketNoiseCache(InBudget, MoveTemp(NewLease)));
	Result->MinPosition = MinCm;
	Result->MaxPosition = MaxCmInclusive;
	Result->Period = PeriodCm;
	Result->Seed = WorldSeed;
	Result->Stage = StageId;
	Result->SaltValue = Salt;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		Result->MinCell[Axis] = MinCellLocal[Axis];
		Result->Extent[Axis] = static_cast<int32>(Extents[Axis]);
	}
	Result->Values.SetNumZeroed(static_cast<int32>(Count));
	Result->Present.SetNumZeroed(static_cast<int32>(Count));
	if (IsCancelled())
	{
		return CacheError(E::Cancelled, TEXT("Pocket cache cancelled"));
	}
	Out = MoveTemp(Result);
	return {};
}

FVoxelLandM5Diagnostic FVoxelLandM5PocketNoiseCache::Sample(
	int64 X, int64 Y, int64 Z, TFunctionRef<bool()> IsCancelled, int32& OutValue)
{
	using E = EVoxelLandM5ErrorCode;
	using namespace VoxelLand::M5;
	if (IsCancelled())
	{
		return CacheError(E::Cancelled, TEXT("Pocket cache cancelled"));
	}
	if (X < MinPosition.X || X > MaxPosition.X || Y < MinPosition.Y || Y > MaxPosition.Y || Z < MinPosition.Z
		|| Z > MaxPosition.Z)
	{
		return CacheError(E::OutOfDomain, TEXT("Pocket sample outside cache bounds"));
	}
	// Eight indexed lookups, floor/weights and seven interpolation steps.
	if (!Budget->TryChargeWork(64))
	{
		return CacheError(E::BudgetExceeded, TEXT("Pocket cache lookup work exhausted"));
	}
	const int64 Position[3] = {X, Y, Z};
	int64 Cell[3], Weight[3];
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (!CheckedMath::TryFloorDivide(Position[Axis], Period, Cell[Axis]))
		{
			return CacheError(E::ArithmeticOverflow, TEXT("Pocket cache coordinate overflow"));
		}
		const int64 Remainder = Position[Axis] - Cell[Axis] * Period;
		Weight[Axis] = (Remainder * FVoxelLandM5PocketNoiseInternals::M5PocketOne) / Period;
	}
	int32 Corners[2][2][2];
	for (int32 K = 0; K < 2; ++K)
	{
		for (int32 J = 0; J < 2; ++J)
		{
			for (int32 I = 0; I < 2; ++I)
			{
				if (IsCancelled())
				{
					return CacheError(E::Cancelled, TEXT("Pocket cache cancelled"));
				}
				const int64 A = Cell[0] + I - MinCell[0];
				const int64 B = Cell[1] + J - MinCell[1];
				const int64 C = Cell[2] + K - MinCell[2];
				if (A < 0 || B < 0 || C < 0 || A >= Extent[0] || B >= Extent[1] || C >= Extent[2])
				{
					return CacheError(E::OutOfDomain, TEXT("Pocket cache corner outside bounds"));
				}
				const int32 Index = static_cast<int32>((C * Extent[1] + B) * Extent[0] + A);
				if (!Present[Index])
				{
					// 512 is the existing per-corner portion of the scalar 4096 allowance.
					if (!Budget->TryChargeWork(512))
					{
						return CacheError(E::BudgetExceeded, TEXT("Pocket cache hash work exhausted"));
					}
					TArray<uint8> Packet, Digest;
					if (!PocketNoise::TryCornerPacket(
							Seed, Stage, SaltValue, Cell[0] + I, Cell[1] + J, Cell[2] + K, Packet)
						|| !VoxelLand::M5::Seed::TrySha256(Packet, Digest) || Digest.Num() != 32)
					{
						return CacheError(E::Unproven, TEXT("Pocket cache hashing failed"));
					}
					Values[Index] = int32(uint32(Digest[0]) | (uint32(Digest[1]) << 8)) - 32768;
					Present[Index] = 1;
				}
				Corners[K][J][I] = Values[Index];
			}
		}
	}
	int32 AlongX[2][2];
	for (int32 K = 0; K < 2; ++K)
	{
		for (int32 J = 0; J < 2; ++J)
		{
			AlongX[K][J] = FVoxelLandM5PocketNoiseInternals::Interpolate(Corners[K][J][0], Corners[K][J][1], Weight[0]);
		}
	}
	const int32 Y0 = FVoxelLandM5PocketNoiseInternals::Interpolate(AlongX[0][0], AlongX[0][1], Weight[1]);
	const int32 Y1 = FVoxelLandM5PocketNoiseInternals::Interpolate(AlongX[1][0], AlongX[1][1], Weight[1]);
	const int32 Value = FVoxelLandM5PocketNoiseInternals::Interpolate(Y0, Y1, Weight[2]);
	if (IsCancelled())
	{
		return CacheError(E::Cancelled, TEXT("Pocket cache cancelled"));
	}
	OutValue = Value;
	return {};
}

namespace VoxelLand::M5::PocketNoise
{
bool TryCornerPacket(
	int64 WorldSeed, const FGuid& StageId, uint64 Salt, int64 CellX, int64 CellY, int64 CellZ, TArray<uint8>& OutPacket)
{
	TArray<uint8> Packet = {'V', 'L', 'M', '5', 'P', 'C', 'K', 0};
	Packet.Reserve(68);
	FVoxelLandM5PocketNoiseInternals::Append32(Packet, 1);
	FVoxelLandM5PocketNoiseInternals::Append64(Packet, static_cast<uint64>(WorldSeed));
	FVoxelLandM5PocketNoiseInternals::Append32(Packet, StageId.A);
	FVoxelLandM5PocketNoiseInternals::Append32(Packet, StageId.B);
	FVoxelLandM5PocketNoiseInternals::Append32(Packet, StageId.C);
	FVoxelLandM5PocketNoiseInternals::Append32(Packet, StageId.D);
	FVoxelLandM5PocketNoiseInternals::Append64(Packet, Salt);
	FVoxelLandM5PocketNoiseInternals::Append64(Packet, static_cast<uint64>(CellX));
	FVoxelLandM5PocketNoiseInternals::Append64(Packet, static_cast<uint64>(CellY));
	FVoxelLandM5PocketNoiseInternals::Append64(Packet, static_cast<uint64>(CellZ));
	OutPacket = MoveTemp(Packet);
	return true;
}

bool TrySample(int64 X, int64 Y, int64 Z, int64 PeriodCm, int64 WorldSeed, const FGuid& StageId, uint64 Salt,
	TFunctionRef<bool()> IsCancelled, int32& OutValue)
{
	if (PeriodCm <= 0 || PeriodCm > FVoxelLandM5PocketNoiseInternals::M5PocketLimitCm
		|| X < -FVoxelLandM5PocketNoiseInternals::M5PocketLimitCm
		|| X > FVoxelLandM5PocketNoiseInternals::M5PocketLimitCm
		|| Y < -FVoxelLandM5PocketNoiseInternals::M5PocketLimitCm
		|| Y > FVoxelLandM5PocketNoiseInternals::M5PocketLimitCm
		|| Z < -FVoxelLandM5PocketNoiseInternals::M5PocketLimitCm
		|| Z > FVoxelLandM5PocketNoiseInternals::M5PocketLimitCm)
	{
		return false;
	}
	const int64 Position[3] = {X, Y, Z};
	int64 Cell[3];
	int64 Weight[3];
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (!CheckedMath::TryFloorDivide(Position[Axis], PeriodCm, Cell[Axis]))
		{
			return false;
		}
		// Validated inputs bound this multiplication and subtraction.
		const int64 Remainder = Position[Axis] - Cell[Axis] * PeriodCm;
		Weight[Axis] = (Remainder * FVoxelLandM5PocketNoiseInternals::M5PocketOne) / PeriodCm;
	}
	int32 Corner[2][2][2];
	for (int32 K = 0; K < 2; ++K)
	{
		for (int32 J = 0; J < 2; ++J)
		{
			for (int32 I = 0; I < 2; ++I)
			{
				if (IsCancelled())
				{
					return false;
				}
				TArray<uint8> Packet;
				TArray<uint8> Digest;
				if (!TryCornerPacket(WorldSeed, StageId, Salt, Cell[0] + I, Cell[1] + J, Cell[2] + K, Packet)
					|| !Seed::TrySha256(Packet, Digest) || Digest.Num() != 32)
				{
					return false;
				}
				Corner[K][J][I] = int32(uint32(Digest[0]) | (uint32(Digest[1]) << 8)) - 32768;
			}
		}
	}
	int32 AlongX[2][2];
	for (int32 K = 0; K < 2; ++K)
	{
		for (int32 J = 0; J < 2; ++J)
		{
			AlongX[K][J] = FVoxelLandM5PocketNoiseInternals::Interpolate(Corner[K][J][0], Corner[K][J][1], Weight[0]);
		}
	}
	const int32 Y0 = FVoxelLandM5PocketNoiseInternals::Interpolate(AlongX[0][0], AlongX[0][1], Weight[1]);
	const int32 Y1 = FVoxelLandM5PocketNoiseInternals::Interpolate(AlongX[1][0], AlongX[1][1], Weight[1]);
	const int32 Value = FVoxelLandM5PocketNoiseInternals::Interpolate(Y0, Y1, Weight[2]);
	if (IsCancelled())
	{
		return false;
	}
	OutValue = Value;
	return true;
}
}
