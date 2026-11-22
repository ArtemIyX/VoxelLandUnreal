#include "Generation/VoxelLandM5CarveMasks.h"

#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CheckedMath.h"

namespace
{
FVoxelLandM5Diagnostic CarveMasksError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Result;
	Result.Code = Code;
	Result.Stage = FName(TEXT("CarveMasks"));
	Result.Message = Message;
	return Result;
}
}

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
		return CarveMasksError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid carve mask dimensions or settings"));
	}
	if (!bWaterPolicySatisfied)
	{
		return CarveMasksError(EVoxelLandM5ErrorCode::Unproven, TEXT("Water policy has not been certified"));
	}
	int64 XY = 0, Samples = 0, OutputXY = 0, Outputs = 0;
	if (!TryMultiplyNonNegative(Size.X, Size.Y, XY) || !TryMultiplyNonNegative(XY, Size.Z, Samples)
		|| !TryMultiplyNonNegative(Count.X, Count.Y, OutputXY) || !TryMultiplyNonNegative(OutputXY, Count.Z, Outputs))
	{
		return CarveMasksError(EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Carve mask grid overflow"));
	}
	if (Samples > MAX_int32 || Outputs > MAX_int32 || Baseline.Num() != Samples || Protection.Num() != Samples
		|| EntranceAuthorized.Num() != Samples)
	{
		return CarveMasksError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Carve mask input size mismatch"));
	}
	int64 Delta = 0, End = 0;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int64 N = Axis == 0 ? Size.X : Axis == 1 ? Size.Y : Size.Z;
		const int64 O = Axis == 0 ? OriginCm.X : Axis == 1 ? OriginCm.Y : OriginCm.Z;
		if (!TryMultiply(N - 1, SpacingCm, Delta) || !TryAdd(O, Delta, End) || !InRange(End))
		{
			return CarveMasksError(
				EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Carve mask grid exceeds coordinate limit"));
		}
	}
	// A mixed cell may contribute geometry up to a cell away from its samples;
	// two extra cells conservatively cover mesher ownership and snap influence.
	const int64 Radius = RoofThicknessCm / SpacingCm + (RoofThicknessCm % SpacingCm != 0 ? 1 : 0) + 2;
	const int64 Halo = Radius + 1;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int64 N = Axis == 0 ? Size.X : Axis == 1 ? Size.Y : Size.Z;
		const int64 S = Axis == 0 ? Start.X : Axis == 1 ? Start.Y : Start.Z;
		const int64 C = Axis == 0 ? Count.X : Axis == 1 ? Count.Y : Count.Z;
		if (S < Halo || S + C > N - Halo)
		{
			return CarveMasksError(EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Incomplete roof and mesher halo"));
		}
	}
	int64 Side = 0, CellsXY = 0, Cells = 0, PerSample = 0, Work = 0, PocketWork = 0, Bytes = 0;
	if (!TryMultiply(Radius, 2, Side) || !TryAdd(Side, 2, Side) || !TryMultiplyNonNegative(Side, Side, CellsXY)
		|| !TryMultiplyNonNegative(CellsXY, Side, Cells) || !TryMultiplyNonNegative(Cells, 12, PerSample)
		|| !TryAdd(PerSample, 8, PerSample)
		|| (bPocketEnabled
			&& (!TryMultiplyNonNegative(25, Size.Z, PocketWork) || !TryMultiplyNonNegative(PocketWork, 3, PocketWork)
				|| !TryAdd(PerSample, PocketWork, PerSample)))
		|| !TryMultiplyNonNegative(Outputs, PerSample, Work) || !TryAdd(Work, 1, Work)
		|| !TryMultiplyNonNegative(Outputs, 3, Bytes) || !TryAdd(Bytes, 4096, Bytes))
	{
		return CarveMasksError(EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Carve mask work or memory overflow"));
	}
	if (IsCancelled())
	{
		return CarveMasksError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Carve mask generation cancelled"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(Bytes, Lease) || !Budget->TryChargeWork(Work))
	{
		return CarveMasksError(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Carve mask admission exhausted"));
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
				const int64 X = Start.X + OX, Y = Start.Y + OY, Z = Start.Z + OZ;
				const int64 Source = X + int64(Size.X) * Y + XY * Z;
				const int64 Dest = OX + int64(Count.X) * OY + OutputXY * OZ;
				if (Protection[Source] > 1 || EntranceAuthorized[Source] > 1)
				{
					return CarveMasksError(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Carve mask input is not binary"));
				}
				bool bRoof = false;
				for (int64 CZ = Z - Radius - 1; CZ <= Z + Radius; ++CZ)
				{
					for (int64 CY = Y - Radius - 1; CY <= Y + Radius; ++CY)
					{
						for (int64 CX = X - Radius - 1; CX <= X + Radius; ++CX)
						{
							if ((Visits++ & 63) == 0 && IsCancelled())
							{
								return CarveMasksError(
									EVoxelLandM5ErrorCode::Cancelled, TEXT("Carve mask generation cancelled"));
							}
							bool bSolid = false, bAir = false;
							for (int32 DZ = 0; DZ < 2; ++DZ)
							{
								for (int32 DY = 0; DY < 2; ++DY)
								{
									for (int32 DX = 0; DX < 2; ++DX)
									{
										const int16 Q =
											Baseline[(CX + DX) + int64(Size.X) * (CY + DY) + XY * (CZ + DZ)];
										bSolid |= Q < 0;
										bAir |= Q >= 0;
									}
								}
							}
							bRoof |= bSolid && bAir;
						}
					}
				}
				bool bDepthAllowed = false;
				if (bPocketEnabled)
				{
					int64 Lowest = MAX_int64, Highest = MIN_int64;
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
									return CarveMasksError(
										EVoxelLandM5ErrorCode::Cancelled, TEXT("Carve mask generation cancelled"));
								}
								const int16 Q = Baseline[(X + DX) + int64(Size.X) * (Y + DY) + XY * K];
								if (Q == 0 || (bAirSeen && Q < 0))
								{
									return CarveMasksError(EVoxelLandM5ErrorCode::Unproven,
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
								return CarveMasksError(EVoxelLandM5ErrorCode::Unproven,
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
				const bool bProtected = Protection[Source] != 0;
				Scratch->Protected[Dest] = bProtected ? 1 : 0;
				Scratch->NetworkAllowed[Dest] = !bProtected && (!bRoof || EntranceAuthorized[Source]) ? 1 : 0;
				Scratch->PocketAllowed[Dest] = !bProtected && !bRoof && bPocketEnabled && bDepthAllowed ? 1 : 0;
			}
		}
	}
	if (IsCancelled())
	{
		return CarveMasksError(EVoxelLandM5ErrorCode::Cancelled, TEXT("Carve mask generation cancelled"));
	}
	OutMasks = MoveTemp(Scratch);
	return {};
}
