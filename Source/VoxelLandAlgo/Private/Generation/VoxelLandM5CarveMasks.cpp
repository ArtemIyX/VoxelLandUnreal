#include "Generation/VoxelLandM5CarveMasks.h"

#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5PocketSurfaceColumns.h"

class FVoxelLandM5CarveMasksInternals final
{
public:
	static FVoxelLandM5Diagnostic CarveMasksError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = FName(TEXT("CarveMasks"));
		Result.Message = Message;
		return Result;
	}
};

FVoxelLandM5CarveMasks::FVoxelLandM5CarveMasks(TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
	: Lease(MoveTemp(InLease))
{
}

FVoxelLandM5Diagnostic FVoxelLandM5CarveMasks::Build(FIntVector Size, const FVoxelLandM5PointCm& OriginCm,
	int64 SpacingCm, FIntVector Start, FIntVector Count, TConstArrayView<int16> Baseline,
	TConstArrayView<uint8> Protection, TConstArrayView<uint8> EntranceAuthorized, int64 RoofThicknessCm,
	bool bPocketEnabled, int64 MinPocketDepthCm, int64 MaxPocketDepthCm, bool bWaterPolicySatisfied,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe>& OutMasks)
{
	return BuildImpl(Size, OriginCm, SpacingCm, Start, Count, Baseline, Protection, EntranceAuthorized,
		RoofThicknessCm, bPocketEnabled, MinPocketDepthCm, MaxPocketDepthCm, bWaterPolicySatisfied,
		Budget, IsCancelled, OutMasks, nullptr);
}

FVoxelLandM5Diagnostic FVoxelLandM5CarveMasks::BuildImpl(FIntVector Size, const FVoxelLandM5PointCm& OriginCm,
	int64 SpacingCm, FIntVector Start, FIntVector Count, TConstArrayView<int16> Baseline,
	TConstArrayView<uint8> Protection, TConstArrayView<uint8> EntranceAuthorized, int64 RoofThicknessCm,
	bool bPocketEnabled, int64 MinPocketDepthCm, int64 MaxPocketDepthCm, bool bWaterPolicySatisfied,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe>& OutMasks, const FVoxelLandM5PocketSurfaceColumns* Columns)
{
	using namespace VoxelLand::M5::CheckedMath;
	constexpr int64 Limit = 1048576;
	const auto InRange = [](int64 V) {
		return V >= -Limit && V <= Limit;
	};
	if (Size.X <= 0 || Size.Y <= 0 || Size.Z <= 0 || Count.X <= 0 || Count.Y <= 0 || Count.Z <= 0 || Start.X < 0
		|| Start.Y < 0 || Start.Z < 0 || SpacingCm <= 0 || SpacingCm > 65536 || RoofThicknessCm < 0
		|| RoofThicknessCm > Limit || MinPocketDepthCm < 0 || MaxPocketDepthCm < MinPocketDepthCm
		|| MaxPocketDepthCm > 2 * Limit || !InRange(OriginCm.X) || !InRange(OriginCm.Y) || !InRange(OriginCm.Z))
	{
		return FVoxelLandM5CarveMasksInternals::CarveMasksError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid carve mask dimensions or settings"));
	}
	if (!bWaterPolicySatisfied)
	{
		return FVoxelLandM5CarveMasksInternals::CarveMasksError(
			EVoxelLandM5ErrorCode::Unproven, TEXT("Water policy has not been certified"));
	}
	int64 XY = 0, Samples = 0, OutputXY = 0, Outputs = 0;
	if (!TryMultiplyNonNegative(Size.X, Size.Y, XY) || !TryMultiplyNonNegative(XY, Size.Z, Samples)
		|| !TryMultiplyNonNegative(Count.X, Count.Y, OutputXY) || !TryMultiplyNonNegative(OutputXY, Count.Z, Outputs))
	{
		return FVoxelLandM5CarveMasksInternals::CarveMasksError(
			EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Carve mask grid overflow"));
	}
	if (Samples > MAX_int32 || Outputs > MAX_int32 || Baseline.Num() != Samples || Protection.Num() != Samples
		|| EntranceAuthorized.Num() != Samples)
	{
		return FVoxelLandM5CarveMasksInternals::CarveMasksError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Carve mask input size mismatch"));
	}
	int64 Delta = 0, End = 0;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int64 N = Axis == 0 ? Size.X : Axis == 1 ? Size.Y : Size.Z;
		const int64 O = Axis == 0 ? OriginCm.X : Axis == 1 ? OriginCm.Y : OriginCm.Z;
		if (!TryMultiply(N - 1, SpacingCm, Delta) || !TryAdd(O, Delta, End) || !InRange(End))
		{
			return FVoxelLandM5CarveMasksInternals::CarveMasksError(
				EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Carve mask grid exceeds coordinate limit"));
		}
	}
	// A mixed or zero-containing cell may contribute geometry up to a cell
	// away from its samples; two extra cells conservatively cover mesher
	// ownership and snap influence.
	const int64 Radius = RoofThicknessCm / SpacingCm + (RoofThicknessCm % SpacingCm != 0 ? 1 : 0) + 2;
	// FieldSafety compares samples at offsets -1..+2 around each proof
	// cell. Inverting that dependency adds two cells on each side of the
	// roof search, including the final corner read.
	const int64 Halo = Radius + 2;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int64 N = Axis == 0 ? Size.X : Axis == 1 ? Size.Y : Size.Z;
		const int64 S = Axis == 0 ? Start.X : Axis == 1 ? Start.Y : Start.Z;
		const int64 C = Axis == 0 ? Count.X : Axis == 1 ? Count.Y : Count.Z;
		if (S < Halo || S + C > N - Halo)
		{
			return FVoxelLandM5CarveMasksInternals::CarveMasksError(
				EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Incomplete roof and mesher halo"));
		}
	}
	int64 Side = 0, PocketWork = 0, ColumnWork = 0, ColumnBytes = 0, Bytes = 0;
	if (!TryMultiply(Radius, 2, Side) || !TryAdd(Side, 4, Side)
		|| (bPocketEnabled && Columns
			&& (!TryMultiplyNonNegative(OutputXY, 75, ColumnWork)
				|| !TryMultiplyNonNegative(OutputXY, 4 * int64(sizeof(int64)), ColumnBytes)
				|| !TryAdd(ColumnBytes, 4096, ColumnBytes)))
		|| (bPocketEnabled && !Columns
			&& (!TryMultiplyNonNegative(25, Size.Z, PocketWork) || !TryMultiplyNonNegative(PocketWork, 3, PocketWork)))
		|| !TryMultiplyNonNegative(Outputs, 3, Bytes) || !TryAdd(Bytes, 4096, Bytes))
	{
		return FVoxelLandM5CarveMasksInternals::CarveMasksError(
			EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Carve mask work or memory overflow"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5CarveMasksInternals::CarveMasksError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Carve mask generation cancelled"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	// Admit the validation scan before touching caller data. The inputs are
	// immutable for this synchronous call, so the count remains valid below.
	if (!Budget->TryReserveBytes(Bytes, Lease) || !Budget->TryChargeWork(Outputs))
	{
		return FVoxelLandM5CarveMasksInternals::CarveMasksError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Carve mask admission exhausted"));
	}
	int64 Unprotected = 0, Scanned = 0;
	for (int64 OZ = 0; OZ < Count.Z; ++OZ)
	{
		for (int64 OY = 0; OY < Count.Y; ++OY)
		{
			for (int64 OX = 0; OX < Count.X; ++OX)
			{
				if ((Scanned++ & 63) == 0 && IsCancelled())
				{
					return FVoxelLandM5CarveMasksInternals::CarveMasksError(
						EVoxelLandM5ErrorCode::Cancelled, TEXT("Carve mask generation cancelled"));
				}
				const int64 Source = (Start.X + OX) + int64(Size.X) * (Start.Y + OY) + XY * (Start.Z + OZ);
				if (Protection[Source] > 1 || EntranceAuthorized[Source] > 1)
				{
					return FVoxelLandM5CarveMasksInternals::CarveMasksError(
						EVoxelLandM5ErrorCode::InvalidInput, TEXT("Carve mask input is not binary"));
				}
				Unprotected += Protection[Source] == 0 ? 1 : 0;
			}
		}
	}
	// Cached owned columns require only one depth comparison per voxel; all
	// 25 extrema queries are charged once per XY and retain both bounds.
	if (bPocketEnabled && Columns) PocketWork = 3;
	int64 Work = 0, BaseWork = 0, SearchWork = 0;
	int64 Grid[3] = {}, Prefix[3] = {}, GridCount = 1, PrefixCount = 1, PrefixBytes = 0;
	if (Unprotected > 0)
	{
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (!TryAdd(Count[Axis], Side - 1, Grid[Axis]) || !TryAdd(Grid[Axis], 1, Prefix[Axis])
				|| !TryMultiplyNonNegative(GridCount, Grid[Axis], GridCount)
				|| !TryMultiplyNonNegative(PrefixCount, Prefix[Axis], PrefixCount))
				return FVoxelLandM5CarveMasksInternals::CarveMasksError(
					EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Carve mask roof grid overflow"));
		}
		if (GridCount > MAX_int32 || PrefixCount > MAX_int32
			|| !TryMultiplyNonNegative(PrefixCount, 2 * int64(sizeof(uint32)), PrefixBytes)
			|| !TryAdd(PrefixBytes, 4096, PrefixBytes)
			|| !TryMultiplyNonNegative(GridCount, 20, SearchWork)
			|| !TryAdd(SearchWork, PrefixCount, SearchWork))
			return FVoxelLandM5CarveMasksInternals::CarveMasksError(
				EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Carve mask roof grid overflow"));
		int64 QueryWork = 0;
		if (!TryMultiplyNonNegative(Unprotected, 8, QueryWork) || !TryAdd(SearchWork, QueryWork, SearchWork))
			return FVoxelLandM5CarveMasksInternals::CarveMasksError(
				EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Carve mask roof query overflow"));
	}
	// Seven output units remain after the charged scan; pocket validation is
	// still required for protected samples because it can return Unproven.
	if (!TryAdd(7, PocketWork, BaseWork) || !TryMultiplyNonNegative(Outputs, BaseWork, Work)
		|| !TryAdd(Work, SearchWork, Work) || !TryAdd(Work, ColumnWork, Work) || !TryAdd(Work, 1, Work))
	{
		return FVoxelLandM5CarveMasksInternals::CarveMasksError(
			EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Carve mask work or memory overflow"));
	}
	if (!Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5CarveMasksInternals::CarveMasksError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Carve mask admission exhausted"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> ColumnLease;
	TArray<int64> ColumnLower, ColumnUpper;
	if (bPocketEnabled && Columns)
	{
		if (OriginCm.X % SpacingCm || OriginCm.Y % SpacingCm || OriginCm.Z % SpacingCm)
			return FVoxelLandM5CarveMasksInternals::CarveMasksError(
				EVoxelLandM5ErrorCode::InvalidInput, TEXT("Pocket window is off lattice"));
		if (!Budget->TryReserveBytes(ColumnBytes, ColumnLease))
			return FVoxelLandM5CarveMasksInternals::CarveMasksError(
				EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Carve mask column admission exhausted"));
		ColumnLower.SetNumUninitialized(int32(OutputXY));
		ColumnUpper.SetNumUninitialized(int32(OutputXY));
		for (int64 OY = 0; OY < Count.Y; ++OY)
			for (int64 OX = 0; OX < Count.X; ++OX)
			{
				if (((OX + int64(Count.X) * OY) & 63) == 0 && IsCancelled())
					return FVoxelLandM5CarveMasksInternals::CarveMasksError(
						EVoxelLandM5ErrorCode::Cancelled, TEXT("Carve mask generation cancelled"));
				int64 Lowest = MAX_int64, Highest = MIN_int64;
				for (int64 DY = -2; DY <= 2; ++DY)
					for (int64 DX = -2; DX <= 2; ++DX)
					{
						int64 Lo = 0, Hi = 0;
						if (!Columns->GetSurfaceBounds(OriginCm.X / SpacingCm + Start.X + OX + DX,
							OriginCm.Y / SpacingCm + Start.Y + OY + DY, Lo, Hi))
							return FVoxelLandM5CarveMasksInternals::CarveMasksError(
								EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Pocket column tile is incomplete"));
						Lowest = FMath::Min(Lowest, Lo); Highest = FMath::Max(Highest, Hi);
					}
				const int32 I = int32(OX + int64(Count.X) * OY);
				ColumnLower[I] = Lowest; ColumnUpper[I] = Highest;
			}
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> PrefixLease;
	TArray<uint32> RoofPrefix;
	if (Unprotected > 0)
	{
		if (!Budget->TryReserveBytes(PrefixBytes, PrefixLease))
			return FVoxelLandM5CarveMasksInternals::CarveMasksError(
				EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Carve mask roof grid admission exhausted"));
		RoofPrefix.Init(0, int32(PrefixCount));
		const int64 StrideY = Prefix[0], StrideZ = Prefix[0] * Prefix[1];
		const auto At = [&](int64 X, int64 Y, int64 Z) -> uint32& {
			return RoofPrefix[int32(X + StrideY * Y + StrideZ * Z)];
		};
		for (int64 Z = 0; Z < Grid[2]; ++Z)
			for (int64 Y = 0; Y < Grid[1]; ++Y)
			{
				if (IsCancelled()) return FVoxelLandM5CarveMasksInternals::CarveMasksError(
					EVoxelLandM5ErrorCode::Cancelled, TEXT("Carve mask generation cancelled"));
				for (int64 X = 0; X < Grid[0]; ++X)
				{
					if ((X & 63) == 0 && IsCancelled()) return FVoxelLandM5CarveMasksInternals::CarveMasksError(
						EVoxelLandM5ErrorCode::Cancelled, TEXT("Carve mask generation cancelled"));
					const int64 CX = Start.X - Halo + X, CY = Start.Y - Halo + Y, CZ = Start.Z - Halo + Z;
					bool bSolid = false, bAir = false, bZero = false;
					for (int32 DZ = 0; DZ < 2; ++DZ)
						for (int32 DY = 0; DY < 2; ++DY)
							for (int32 DX = 0; DX < 2; ++DX)
							{
								const int16 Q = Baseline[(CX + DX) + int64(Size.X) * (CY + DY) + XY * (CZ + DZ)];
								bSolid |= Q < 0; bAir |= Q >= 0; bZero |= Q == 0;
							}
					const int64 A = X + 1, B = Y + 1, C = Z + 1;
					const int64 Sum = int64((bSolid && bAir) || bZero) + int64(At(A - 1, B, C))
						+ int64(At(A, B - 1, C)) + int64(At(A, B, C - 1))
						- int64(At(A - 1, B - 1, C)) - int64(At(A - 1, B, C - 1))
						- int64(At(A, B - 1, C - 1)) + int64(At(A - 1, B - 1, C - 1));
					if (Sum < 0 || Sum > MAX_uint32) return FVoxelLandM5CarveMasksInternals::CarveMasksError(
						EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Carve mask roof prefix overflow"));
					At(A, B, C) = uint32(Sum);
				}
			}
	}
	TSharedPtr<FVoxelLandM5CarveMasks, ESPMode::ThreadSafe> Scratch =
		MakeShareable(new FVoxelLandM5CarveMasks(MoveTemp(Lease)));
	Scratch->NetworkAllowed.SetNumUninitialized(int32(Outputs));
	Scratch->PocketAllowed.SetNumUninitialized(int32(Outputs));
	Scratch->Protected.SetNumUninitialized(int32(Outputs));
	int64 Visits = 0;
	for (int64 OZ = 0; OZ < Count.Z; ++OZ)
	{
		for (int64 OY = 0; OY < Count.Y; ++OY)
		{
			for (int64 OX = 0; OX < Count.X; ++OX)
			{
				if ((Visits++ & 63) == 0 && IsCancelled())
				{
					return FVoxelLandM5CarveMasksInternals::CarveMasksError(
						EVoxelLandM5ErrorCode::Cancelled, TEXT("Carve mask generation cancelled"));
				}
				const int64 X = Start.X + OX, Y = Start.Y + OY, Z = Start.Z + OZ;
				const int64 Source = X + int64(Size.X) * Y + XY * Z;
				const int64 Dest = OX + int64(Count.X) * OY + OutputXY * OZ;
				const bool bProtected = Protection[Source] != 0;
				bool bRoof = false;
				if (!bProtected)
				{
					const int64 StrideY = Prefix[0], StrideZ = Prefix[0] * Prefix[1];
					const auto At = [&](int64 PX, int64 PY, int64 PZ) -> int64 {
						return RoofPrefix[int32(PX + StrideY * PY + StrideZ * PZ)];
					};
					const int64 A = OX, B = OY, C = OZ, D = OX + Side, E = OY + Side, F = OZ + Side;
					const int64 Covered = At(D, E, F) - At(A, E, F) - At(D, B, F) - At(D, E, C)
						+ At(A, B, F) + At(A, E, C) + At(D, B, C) - At(A, B, C);
					if (Covered < 0) return FVoxelLandM5CarveMasksInternals::CarveMasksError(
						EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Carve mask roof query underflow"));
					bRoof = Covered != 0;
				}
				bool bDepthAllowed = false;
				if (bPocketEnabled)
				{
					int64 Lowest = MAX_int64, Highest = MIN_int64;
					if (Columns)
					{
						const int32 ColumnIndex = int32(OX + int64(Count.X) * OY);
						Lowest = ColumnLower[ColumnIndex]; Highest = ColumnUpper[ColumnIndex];
						const int64 SampleZ = OriginCm.Z + Z * SpacingCm;
						bDepthAllowed = Lowest - SampleZ >= MinPocketDepthCm && Highest - SampleZ <= MaxPocketDepthCm;
					}
					else
					{
					for (int64 DY = -2; DY <= 2; ++DY)
					{
						for (int64 DX = -2; DX <= 2; ++DX)
						{
							bool bAirSeen = false;
							int64 Crossing = -1;
							for (int64 K = 0; K < Size.Z; ++K)
							{
								if ((Visits++ & 63) == 0 && IsCancelled())
								{
									return FVoxelLandM5CarveMasksInternals::CarveMasksError(
										EVoxelLandM5ErrorCode::Cancelled, TEXT("Carve mask generation cancelled"));
								}
								const int16 Q = Baseline[(X + DX) + int64(Size.X) * (Y + DY) + XY * K];
								if (Q == 0 || (bAirSeen && Q < 0))
								{
									return FVoxelLandM5CarveMasksInternals::CarveMasksError(
										EVoxelLandM5ErrorCode::Unproven,
										TEXT("Pocket surface is ambiguous or has multiple crossings"));
								}
								if (Q > 0 && !bAirSeen)
								{
									Crossing = K - 1;
									bAirSeen = true;
								}
							}
							if (Crossing < 1 || Crossing + 2 >= Size.Z)
							{
								return FVoxelLandM5CarveMasksInternals::CarveMasksError(EVoxelLandM5ErrorCode::Unproven,
									TEXT("Pocket surface lacks an enclosed vertical halo"));
							}
							Lowest = FMath::Min(Lowest, Crossing - 1);
							Highest = FMath::Max(Highest, Crossing + 2);
						}
					}
					const int64 LowerDepth = (Lowest - Z) * SpacingCm;
					const int64 UpperDepth = (Highest - Z) * SpacingCm;
					bDepthAllowed = LowerDepth >= MinPocketDepthCm && UpperDepth <= MaxPocketDepthCm;
					}
				}
				Scratch->Protected[Dest] = bProtected ? 1 : 0;
				Scratch->NetworkAllowed[Dest] = !bProtected && (!bRoof || EntranceAuthorized[Source]) ? 1 : 0;
				Scratch->PocketAllowed[Dest] = !bProtected && !bRoof && bPocketEnabled && bDepthAllowed ? 1 : 0;
			}
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandM5CarveMasksInternals::CarveMasksError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Carve mask generation cancelled"));
	}
	OutMasks = MoveTemp(Scratch);
	return {};
}
