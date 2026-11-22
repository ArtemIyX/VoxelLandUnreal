#include "Generation/VoxelLandM5SurfaceQuery.h"

#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5CheckedMath.h"

class FVoxelLandM5SurfaceQueryInternals final
{
public:
	static FVoxelLandM5Diagnostic SurfaceQueryError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = FName(TEXT("SurfaceQuery"));
		Result.Message = Message;
		return Result;
	}
};

FVoxelLandM5Diagnostic VoxelLand::M5::SurfaceQuery::QueryFlatSurface(
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, FIntVector SampleCounts,
	const FVoxelLandM5PointCm& Origin, int64 SpacingCm, TConstArrayView<int16> Samples,
	TConstArrayView<uint8> ProtectionColumns, int64 X, int64 Y, int32 RadiusCm, bool bHasWaterLevel, int64 WaterLevelCm,
	int64 MaxSamplesToInspect, TFunctionRef<bool()> IsCancelled, int64& OutSurfaceZ, int64& OutMinSurfaceZ,
	int64& OutMaxSurfaceZ, int32& OutSlopeDegrees, bool& bOutProtected, bool& bOutFlooded)
{
	using namespace VoxelLand::M5::CheckedMath;
	constexpr int64 QueryCoordinateLimit = 1048576;
	const auto InRange = [](int64 Value) {
		return Value >= -QueryCoordinateLimit && Value <= QueryCoordinateLimit;
	};
	if (SampleCounts.X < 6 || SampleCounts.Y < 6 || SampleCounts.Z < 2 || SpacingCm <= 0 || SpacingCm > 65536
		|| SpacingCm % 2 != 0 || Origin.X % SpacingCm != 0 || Origin.Y % SpacingCm != 0 || Origin.Z % SpacingCm != 0
		|| RadiusCm < 0 || MaxSamplesToInspect < 0 || !InRange(Origin.X) || !InRange(Origin.Y) || !InRange(Origin.Z)
		|| !InRange(X) || !InRange(Y) || (bHasWaterLevel && !InRange(WaterLevelCm)))
	{
		return FVoxelLandM5SurfaceQueryInternals::SurfaceQueryError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid flat surface query input"));
	}
	int64 XY = 0;
	int64 Count = 0;
	if (!TryMultiplyNonNegative(SampleCounts.X, SampleCounts.Y, XY)
		|| !TryMultiplyNonNegative(XY, SampleCounts.Z, Count))
	{
		return FVoxelLandM5SurfaceQueryInternals::SurfaceQueryError(
			EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Surface grid size overflow"));
	}
	if (Count > MAX_int32 || Samples.Num() != Count || ProtectionColumns.Num() != XY)
	{
		return FVoxelLandM5SurfaceQueryInternals::SurfaceQueryError(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Surface grid or protection size mismatch"));
	}
	int64 EndX = 0, EndY = 0, EndZ = 0, Delta = 0;
	if (!TryMultiply(SampleCounts.X - 1, SpacingCm, Delta) || !TryAdd(Origin.X, Delta, EndX)
		|| !TryMultiply(SampleCounts.Y - 1, SpacingCm, Delta) || !TryAdd(Origin.Y, Delta, EndY)
		|| !TryMultiply(SampleCounts.Z - 1, SpacingCm, Delta) || !TryAdd(Origin.Z, Delta, EndZ) || !InRange(EndX)
		|| !InRange(EndY) || !InRange(EndZ))
	{
		return FVoxelLandM5SurfaceQueryInternals::SurfaceQueryError(
			EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Surface grid exceeds supported coordinates"));
	}
	int64 MinX = 0, MaxX = 0, MinY = 0, MaxY = 0;
	if (!TrySubtract(X, RadiusCm, MinX) || !TryAdd(X, RadiusCm, MaxX) || !TrySubtract(Y, RadiusCm, MinY)
		|| !TryAdd(Y, RadiusCm, MaxY) || MinX < Origin.X || MaxX > EndX || MinY < Origin.Y || MaxY > EndY)
	{
		return FVoxelLandM5SurfaceQueryInternals::SurfaceQueryError(
			EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Surface footprint is outside the grid"));
	}
	// Differences are nonnegative after domain checks, so integer division is floor.
	const int64 LowX = (MinX - Origin.X) / SpacingCm - 2;
	const int64 HighX = (MaxX - Origin.X) / SpacingCm + 3;
	const int64 LowY = (MinY - Origin.Y) / SpacingCm - 2;
	const int64 HighY = (MaxY - Origin.Y) / SpacingCm + 3;
	if (LowX < 0 || LowY < 0 || HighX >= SampleCounts.X || HighY >= SampleCounts.Y)
	{
		return FVoxelLandM5SurfaceQueryInternals::SurfaceQueryError(
			EVoxelLandM5ErrorCode::OutOfDomain, TEXT("Surface query needs a complete mesher halo"));
	}
	int64 Columns = 0, Work = 0;
	if (!TryMultiplyNonNegative(HighX - LowX + 1, HighY - LowY + 1, Columns)
		|| !TryMultiplyNonNegative(Columns, SampleCounts.Z, Work))
	{
		return FVoxelLandM5SurfaceQueryInternals::SurfaceQueryError(
			EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Surface query work overflow"));
	}
	if (Work > MaxSamplesToInspect)
	{
		return FVoxelLandM5SurfaceQueryInternals::SurfaceQueryError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Surface query work budget exceeded"));
	}
	int64 ColumnWork = 0, ChargedWork = 0;
	if (!TryMultiplyNonNegative(Columns, 3, ColumnWork) || !TryAdd(Work, ColumnWork, ChargedWork)
		|| !TryAdd(ChargedWork, 1, ChargedWork))
	{
		return FVoxelLandM5SurfaceQueryInternals::SurfaceQueryError(
			EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Surface query work charge overflow"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5SurfaceQueryInternals::SurfaceQueryError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Surface query cancelled"));
	}
	if (!Budget->TryChargeWork(ChargedWork))
	{
		return FVoxelLandM5SurfaceQueryInternals::SurfaceQueryError(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Shared surface work budget exceeded"));
	}
	int64 CommonCrossing = -1;
	int16 CommonBelow = 0, CommonAbove = 0;
	bool bProtected = false;
	int64 Inspected = 0;
	for (int64 CY = LowY; CY <= HighY; ++CY)
	{
		for (int64 CX = LowX; CX <= HighX; ++CX)
		{
			const int64 Column = CX + int64(SampleCounts.X) * CY;
			if (ProtectionColumns[Column] > 1)
			{
				return FVoxelLandM5SurfaceQueryInternals::SurfaceQueryError(
					EVoxelLandM5ErrorCode::InvalidInput, TEXT("Protection column is not binary"));
			}
			bProtected |= ProtectionColumns[Column] != 0;
			int64 Crossing = -1;
			bool bAirSeen = false;
			for (int64 Z = 0; Z < SampleCounts.Z; ++Z)
			{
				if ((Inspected++ & 63) == 0 && IsCancelled())
				{
					return FVoxelLandM5SurfaceQueryInternals::SurfaceQueryError(
						EVoxelLandM5ErrorCode::Cancelled, TEXT("Surface query cancelled"));
				}
				const int16 Q = Samples[Column + XY * Z];
				if (Q == 0 || (bAirSeen && Q < 0))
				{
					return FVoxelLandM5SurfaceQueryInternals::SurfaceQueryError(
						EVoxelLandM5ErrorCode::Unproven, TEXT("Ambiguous or multiple surface crossings"));
				}
				if (Q > 0 && !bAirSeen)
				{
					Crossing = Z - 1;
					bAirSeen = true;
				}
			}
			if (Crossing < 0 || !bAirSeen)
			{
				return FVoxelLandM5SurfaceQueryInternals::SurfaceQueryError(
					EVoxelLandM5ErrorCode::Unproven, TEXT("Surface crossing not enclosed vertically"));
			}
			const int16 Below = Samples[Column + XY * Crossing];
			const int16 Above = Samples[Column + XY * (Crossing + 1)];
			if (int32(Below) != -int32(Above)
				|| (CommonCrossing >= 0
					&& (Crossing != CommonCrossing || Below != CommonBelow || Above != CommonAbove)))
			{
				return FVoxelLandM5SurfaceQueryInternals::SurfaceQueryError(
					EVoxelLandM5ErrorCode::Unproven, TEXT("Surface is not a certified flat midpoint"));
			}
			CommonCrossing = Crossing;
			CommonBelow = Below;
			CommonAbove = Above;
		}
	}
	int64 Offset = 0, Surface = 0;
	if (!TryMultiply(CommonCrossing, SpacingCm, Offset) || !TryAdd(Offset, SpacingCm / 2, Offset)
		|| !TryAdd(Origin.Z, Offset, Surface) || !InRange(Surface))
	{
		return FVoxelLandM5SurfaceQueryInternals::SurfaceQueryError(
			EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Surface height overflow"));
	}
	if (IsCancelled())
	{
		return FVoxelLandM5SurfaceQueryInternals::SurfaceQueryError(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Surface query cancelled"));
	}
	OutSurfaceZ = OutMinSurfaceZ = OutMaxSurfaceZ = Surface;
	OutSlopeDegrees = 0;
	bOutProtected = bProtected;
	bOutFlooded = bHasWaterLevel && WaterLevelCm >= Surface;
	return {};
}
