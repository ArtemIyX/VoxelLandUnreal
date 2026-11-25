#include "Generation/Caves/Geometry/VoxelLandCaveProfileGeometry.h"

#include "Generation/Caves/Geometry/VoxelLandCaveIntegerGeometry.h"

namespace VoxelLand::M5::ProfileGeometry
{
namespace
{
constexpr int64 Limit = 1048576;

bool InRange(int64 Value)
{
	return Value >= -Limit && Value <= Limit;
}
bool ValidPoint(const FVoxelLandM5PointCm& P)
{
	return InRange(P.X) && InRange(P.Y) && InRange(P.Z);
}

// Round the complete interpolated height, ties away from zero, without a
// potentially overflowing product. Numerator is in [0, Denominator].
int64 InterpolateHeight(int64 StartQ8, int64 DeltaQ8, uint64 Numerator, uint64 Denominator)
{
	const bool bNegative = DeltaQ8 < 0;
	const uint64 Magnitude = uint64(bNegative ? -DeltaQ8 : DeltaQ8);
	uint64 Quotient = 0, Remainder = 0;
	for (int32 Bit = 29; Bit >= 0; --Bit)
	{
		Quotient *= 2;
		Remainder = Remainder * 2 + (((Magnitude >> Bit) & 1) ? Numerator : 0);
		Quotient += Remainder / Denominator;
		Remainder %= Denominator;
	}
	const int64 Integral = StartQ8 + (bNegative ? -int64(Quotient) : int64(Quotient));
	const uint64 TwiceRemainder = Remainder * 2;
	const bool bAwayOnTie = Integral == 0 || (Integral < 0) == bNegative;
	if (TwiceRemainder > Denominator || (TwiceRemainder == Denominator && bAwayOnTie))
	{
		return Integral + (bNegative ? -1 : 1);
	}
	return Integral;
}
}

bool TrySegmentDistanceQ8(const FSegment& S, const FVoxelLandM5PointCm& P, int64& OutDistanceQ8)
{
	if (S.Version != 2 || !ValidPoint(P) || !ValidPoint(S.StartCm) || !ValidPoint(S.EndCm) || S.RadiusCm < 1
		|| S.RadiusCm > Limit || !InRange(S.StartFloorCm) || !InRange(S.EndFloorCm) || !InRange(S.StartCeilingCm)
		|| !InRange(S.EndCeilingCm) || S.StartFloorCm >= S.StartCeilingCm || S.EndFloorCm >= S.EndCeilingCm)
	{
		return false;
	}
	const int64 DX = S.EndCm.X - S.StartCm.X, DY = S.EndCm.Y - S.StartCm.Y;
	const uint64 LengthSquared = uint64(DX * DX + DY * DY);
	if (LengthSquared == 0)
	{
		return false;
	}
	const int64 Projection = (P.X - S.StartCm.X) * DX + (P.Y - S.StartCm.Y) * DY;
	const uint64 Numerator = Projection <= 0 ? 0 : FMath::Min(uint64(Projection), LengthSquared);
	FVoxelLandM5PointCm FlatP = P, FlatA = S.StartCm, FlatB = S.EndCm;
	FlatP.Z = FlatA.Z = FlatB.Z = 0;
	int64 LateralQ8 = 0;
	if (!IntegerGeometry::TrySegmentDistanceQ8(FlatP, FlatA, FlatB, S.RadiusCm, LateralQ8))
	{
		return false;
	}
	const int64 FloorQ8 =
		InterpolateHeight(S.StartFloorCm * 256, (S.EndFloorCm - S.StartFloorCm) * 256, Numerator, LengthSquared);
	const int64 CeilingQ8 =
		InterpolateHeight(S.StartCeilingCm * 256, (S.EndCeilingCm - S.StartCeilingCm) * 256, Numerator, LengthSquared);
	OutDistanceQ8 = FMath::Max(LateralQ8, FMath::Max(FloorQ8 - P.Z * 256, P.Z * 256 - CeilingQ8));
	return true;
}

bool TryChamberDistanceQ8(const FChamber& C, const FVoxelLandM5PointCm& P, int64& OutDistanceQ8)
{
	if (C.Version != 2 || !ValidPoint(P) || !ValidPoint(C.CenterCm) || C.RadiusCm < 1 || C.RadiusCm > Limit
		|| !InRange(C.FloorCm) || !InRange(C.CeilingCm) || C.FloorCm >= C.CeilingCm)
	{
		return false;
	}
	FVoxelLandM5PointCm FlatP = P, FlatCenter = C.CenterCm;
	FlatP.Z = FlatCenter.Z = 0;
	int64 LateralQ8 = 0;
	if (!IntegerGeometry::TrySphereDistanceQ8(FlatP, FlatCenter, C.RadiusCm, LateralQ8))
	{
		return false;
	}
	OutDistanceQ8 = FMath::Max(LateralQ8, FMath::Max((C.FloorCm - P.Z) * 256, (P.Z - C.CeilingCm) * 256));
	return true;
}
}
