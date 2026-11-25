#include "Generation/Caves/Geometry/VoxelLandCaveIntegerGeometry.h"

#include "Data/Caves/Geometry/VoxelLandCavePointCm.h"

class FVoxelLandM5IntegerGeometryInternals final
{
public:
	static constexpr int64 M5IntegerCoordinateLimit = 1048576;
	static constexpr uint64 WordMask = 0xffffffffULL;

	// High and low words, respectively. Only bounded unsigned operations are used.
	using FWide = TPair<uint64, uint64>;

	static bool ValidPoint(const FVoxelLandM5PointCm& P)
	{
		return P.X >= -M5IntegerCoordinateLimit && P.X <= M5IntegerCoordinateLimit && P.Y >= -M5IntegerCoordinateLimit
			&& P.Y <= M5IntegerCoordinateLimit && P.Z >= -M5IntegerCoordinateLimit && P.Z <= M5IntegerCoordinateLimit;
	}

	static bool ValidRadius(int64 Radius) { return Radius > 0 && Radius <= M5IntegerCoordinateLimit; }

	static FWide Multiply64(uint64 A, uint64 B)
	{
		const uint64 A0 = A & WordMask;
		const uint64 A1 = A >> 32;
		const uint64 B0 = B & WordMask;
		const uint64 B1 = B >> 32;
		const uint64 T0 = A0 * B0;
		const uint64 T1 = A1 * B0 + (T0 >> 32);
		const uint64 T2 = A0 * B1 + (T1 & WordMask);
		return FWide(A1 * B1 + (T1 >> 32) + (T2 >> 32), (T2 << 32) | (T0 & WordMask));
	}

	static bool Less(const FWide& A, const FWide& B) { return A.Key < B.Key || (A.Key == B.Key && A.Value < B.Value); }

	static bool TrySubtractWide(const FWide& A, const FWide& B, FWide& Out)
	{
		if (Less(A, B))
		{
			return false;
		}
		Out = FWide(A.Key - B.Key - static_cast<uint64>(A.Value < B.Value), A.Value - B.Value);
		return true;
	}

	static bool TryShift16(const FWide& A, FWide& Out)
	{
		if ((A.Key >> 48) != 0)
		{
			return false;
		}
		Out = FWide((A.Key << 16) | (A.Value >> 48), A.Value << 16);
		return true;
	}

	// Determine floor(sqrt(N / Denominator)) by comparing exact 128-bit products.
	// Within the checked coordinate envelope sqrt(N / Denominator) < 2^30.
	static bool TryDistanceRoot(const FWide& N, uint64 Denominator, uint64& Out)
	{
		if (Denominator == 0 || !Less(N, Multiply64(uint64(1) << 60, Denominator)))
		{
			return false;
		}
		uint64 Low = 0;
		uint64 High = uint64(1) << 30;
		while (Low + 1 < High)
		{
			const uint64 Mid = Low + (High - Low) / 2;
			if (!Less(N, Multiply64(Mid * Mid, Denominator)))
			{
				Low = Mid;
			}
			else
			{
				High = Mid;
			}
		}
		Out = Low;
		return true;
	}

	static uint64 SquaredDistance(const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B)
	{
		// Caller checked the +/- 2^20 cm envelope: each difference is bounded by
		// 2^21, and the sum of three squares is at most 3 * 2^42.
		const int64 X = A.X - B.X;
		const int64 Y = A.Y - B.Y;
		const int64 Z = A.Z - B.Z;
		return static_cast<uint64>(X * X + Y * Y + Z * Z);
	}

	static bool TryPointDistance(uint64 SquaredCm, uint64& Out)
	{
		FWide Shifted;
		return TryShift16(FWide(0, SquaredCm), Shifted) && TryDistanceRoot(Shifted, 1, Out);
	}
};

namespace VoxelLand::M5::IntegerGeometry
{
bool TrySphereDistanceQ8(const FVoxelLandM5PointCm& P, const FVoxelLandM5PointCm& Center, int64 RadiusCm, int64& Out)
{
	if (!FVoxelLandM5IntegerGeometryInternals::ValidPoint(P)
		|| !FVoxelLandM5IntegerGeometryInternals::ValidPoint(Center)
		|| !FVoxelLandM5IntegerGeometryInternals::ValidRadius(RadiusCm))
	{
		return false;
	}
	uint64 Distance;
	if (!FVoxelLandM5IntegerGeometryInternals::TryPointDistance(
			FVoxelLandM5IntegerGeometryInternals::SquaredDistance(P, Center), Distance))
	{
		return false;
	}
	Out = static_cast<int64>(Distance) - RadiusCm * 256;
	return true;
}

bool TrySegmentDistanceQ8(const FVoxelLandM5PointCm& P, const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B,
	int64 RadiusCm, int64& Out)
{
	if (!FVoxelLandM5IntegerGeometryInternals::ValidPoint(P) || !FVoxelLandM5IntegerGeometryInternals::ValidPoint(A)
		|| !FVoxelLandM5IntegerGeometryInternals::ValidPoint(B)
		|| !FVoxelLandM5IntegerGeometryInternals::ValidRadius(RadiusCm))
	{
		return false;
	}
	const int64 Vx = B.X - A.X;
	const int64 Vy = B.Y - A.Y;
	const int64 Vz = B.Z - A.Z;
	const int64 Wx = P.X - A.X;
	const int64 Wy = P.Y - A.Y;
	const int64 Wz = P.Z - A.Z;
	const uint64 LengthSquared = static_cast<uint64>(Vx * Vx + Vy * Vy + Vz * Vz);
	if (LengthSquared == 0)
	{
		return false;
	}
	const int64 Projection = Wx * Vx + Wy * Vy + Wz * Vz;
	uint64 Distance;
	if (Projection <= 0)
	{
		if (!FVoxelLandM5IntegerGeometryInternals::TryPointDistance(
				FVoxelLandM5IntegerGeometryInternals::SquaredDistance(P, A), Distance))
		{
			return false;
		}
	}
	else if (static_cast<uint64>(Projection) >= LengthSquared)
	{
		if (!FVoxelLandM5IntegerGeometryInternals::TryPointDistance(
				FVoxelLandM5IntegerGeometryInternals::SquaredDistance(P, B), Distance))
		{
			return false;
		}
	}
	else
	{
		const uint64 D = static_cast<uint64>(Projection);
		FVoxelLandM5IntegerGeometryInternals::FWide Numerator;
		FVoxelLandM5IntegerGeometryInternals::FWide Shifted;
		if (!FVoxelLandM5IntegerGeometryInternals::TrySubtractWide(
				FVoxelLandM5IntegerGeometryInternals::Multiply64(
					FVoxelLandM5IntegerGeometryInternals::SquaredDistance(P, A), LengthSquared),
				FVoxelLandM5IntegerGeometryInternals::Multiply64(D, D), Numerator)
			|| !FVoxelLandM5IntegerGeometryInternals::TryShift16(Numerator, Shifted)
			|| !FVoxelLandM5IntegerGeometryInternals::TryDistanceRoot(Shifted, LengthSquared, Distance))
		{
			return false;
		}
	}
	Out = static_cast<int64>(Distance) - RadiusCm * 256;
	return true;
}
}
