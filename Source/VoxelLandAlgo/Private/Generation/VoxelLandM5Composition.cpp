#include "Generation/VoxelLandM5Composition.h"

#include "Data/Structs/VoxelLandM5CavePlan.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5IntegerGeometry.h"

namespace VoxelLand::M5::Composition
{
class FVoxelLandM5CompositionInternals final
{
public:
	static bool TryContribution(int64 DistanceQ8, int32 VoxelSizeCm, int16& Out)
	{
		int64 Negated = 0;
		int64 Numerator = 0;
		if (!CheckedMath::TrySubtract(0, DistanceQ8, Negated) || !CheckedMath::TryMultiply(Negated, 4, Numerator))
		{
			return false;
		}
		int64 Quotient = Numerator / VoxelSizeCm;
		const int64 Remainder = Numerator % VoxelSizeCm;
		const int64 AbsoluteRemainder = Remainder < 0 ? -Remainder : Remainder;
		const int64 HalfRoundedUp = (static_cast<int64>(VoxelSizeCm) + 1) / 2;
		if (AbsoluteRemainder >= HalfRoundedUp && Remainder != 0)
		{
			if (!CheckedMath::TryAdd(Quotient, Numerator < 0 ? -1 : 1, Quotient))
			{
				return false;
			}
		}
		Out = static_cast<int16>(FMath::Clamp(Quotient, static_cast<int64>(MIN_int16), static_cast<int64>(MAX_int16)));
		return true;
	}
};

bool TryCompose(int16 M4Density, const FVoxelLandM5CavePlan& Plan, const FVoxelLandM5PointCm& PositionCm,
	int32 VoxelSizeCm, bool bEnabled, bool bCarveAllowed, bool bProtected, bool bHasPocket, int64 PocketDistanceQ8,
	TFunctionRef<bool()> IsCancelled, int16& Out)
{
	if (IsCancelled())
	{
		return false;
	}
	if (!bEnabled || !bCarveAllowed || bProtected)
	{
		Out = M4Density;
		return true;
	}
	if (Plan.GeometryVersion != 1 || VoxelSizeCm <= 0 || PositionCm.X < -1048576 || PositionCm.X > 1048576 || PositionCm.Y < -1048576
		|| PositionCm.Y > 1048576 || PositionCm.Z < -1048576 || PositionCm.Z > 1048576)
	{
		return false;
	}

	int16 Result = M4Density;
	auto AddDistance = [&Result, VoxelSizeCm](int64 DistanceQ8) {
		int16 Contribution = 0;
		if (!FVoxelLandM5CompositionInternals::TryContribution(DistanceQ8, VoxelSizeCm, Contribution))
		{
			return false;
		}
		Result = FMath::Max(Result, Contribution);
		return true;
	};
	uint64 Processed = 0;
	for (const FVoxelLandM5Chamber& Chamber : Plan.Chambers)
	{
		if ((Processed++ % 64 == 0 && IsCancelled()) || !Chamber.bHasId
			|| Chamber.Profile.Version != Plan.GeometryVersion)
		{
			return false;
		}
		int64 DistanceQ8 = 0;
		if (!IntegerGeometry::TrySphereDistanceQ8(PositionCm, Chamber.CenterCm, Chamber.RadiusCm, DistanceQ8)
			|| !AddDistance(DistanceQ8))
		{
			return false;
		}
	}
	for (const FVoxelLandM5Segment& Segment : Plan.Segments)
	{
		if ((Processed++ % 64 == 0 && IsCancelled()) || !Segment.bHasId || !Segment.bHasConnectionId
			|| Segment.Profile.Version != Plan.GeometryVersion)
		{
			return false;
		}
		int64 DistanceQ8 = 0;
		if (!IntegerGeometry::TrySegmentDistanceQ8(
				PositionCm, Segment.StartCm, Segment.EndCm, Segment.RadiusCm, DistanceQ8)
			|| !AddDistance(DistanceQ8))
		{
			return false;
		}
	}
	if ((bHasPocket && !AddDistance(PocketDistanceQ8)) || IsCancelled())
	{
		return false;
	}
	Out = Result;
	return true;
}
}
