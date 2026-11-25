#include "Generation/VoxelLandM5ProfileContainment.h"

#include "Data/Structs/VoxelLandM5CapsuleProfile.h"
#include "Data/Structs/VoxelLandM5PointCm.h"
#include "Data/Structs/VoxelLandM5Segment.h"
#include "Generation/VoxelLandM5FeetProposal.h"

class FVoxelLandM5ProfileContainmentInternals final
{
public:
	static constexpr int64 CoordinateLimit = 1048576;
	static constexpr int64 ProjectionBoundFactor = 2;
	static constexpr int64 DegreesPerRightAngle = 90;
	static constexpr int64 Q8RoundingAllowanceCm = 1;

	static bool InRange(const int64 Value)
	{
		return Value >= -CoordinateLimit && Value <= CoordinateLimit;
	}

	static bool PointInRange(const FVoxelLandM5PointCm& Point)
	{
		return InRange(Point.X) && InRange(Point.Y) && InRange(Point.Z);
	}

	// Projection is clamped, so perturbing XY by an inflated capsule radius R
	// changes its parameter by at most 2*R/Linf. The extra centimetre bounds
	// Q8 interpolation rounding outwards.
	static int64 CalculateSlack(const int64 Start, const int64 End, const int64 Radius, const int64 Horizontal)
	{
		const int64 Difference = FMath::Abs(End - Start);
		if (Difference == 0)
		{
			return 0;
		}
		// Validated inputs bound this numerator below 2^44.
		const int64 Numerator = ProjectionBoundFactor * Radius * Difference;
		return Numerator / Horizontal + (Numerator % Horizontal != 0 ? 1 : 0) + Q8RoundingAllowanceCm;
	}

	static bool EnvelopeInRange(const FVoxelLandM5PointCm& Point, const int64 Radius,
		const int64 Margin, const int64 Height)
	{
		return InRange(Point.X - Radius) && InRange(Point.X + Radius) && InRange(Point.Y - Radius)
			&& InRange(Point.Y + Radius) && InRange(Point.Z - Margin) && InRange(Point.Z + Height + Margin);
	}

	static EVoxelLandM5ErrorCode CalculateBounds(const FVoxelLandM5Segment& Segment,
		const FVoxelLandM5CapsuleProfile& Capsule, int64& OutFloorSlack, int64& OutCeilingSlack)
	{
		using E = EVoxelLandM5ErrorCode;
		if (Segment.Profile.Version != 2)
		{
			return E::UnsupportedVersion;
		}
		if (!Segment.bHasId || Segment.RadiusCm < 1 || Segment.RadiusCm > CoordinateLimit
			|| Capsule.RadiusCm < 1 || Capsule.RadiusCm > CoordinateLimit
			|| Capsule.StandingHeightCm < 2 * int64(Capsule.RadiusCm) || Capsule.StandingHeightCm > CoordinateLimit
			|| Capsule.ClearanceMarginCm < 0 || Capsule.ClearanceMarginCm > CoordinateLimit
			|| Capsule.MaxSlopeDegrees < 0 || Capsule.MaxSlopeDegrees > DegreesPerRightAngle || Capsule.MaxStepCm < 0)
		{
			return E::InvalidInput;
		}
		if (!PointInRange(Segment.StartCm) || !PointInRange(Segment.EndCm)
			|| !InRange(Segment.Profile.StartFloorCm) || !InRange(Segment.Profile.EndFloorCm)
			|| !InRange(Segment.Profile.StartCeilingCm) || !InRange(Segment.Profile.EndCeilingCm))
		{
			return E::OutOfDomain;
		}
		if (Segment.Profile.StartFloorCm >= Segment.Profile.StartCeilingCm
			|| Segment.Profile.EndFloorCm >= Segment.Profile.EndCeilingCm)
		{
			return E::InvalidInput;
		}
		const int64 DX = Segment.EndCm.X - Segment.StartCm.X;
		const int64 DY = Segment.EndCm.Y - Segment.StartCm.Y;
		const int64 Horizontal = FMath::Max(FMath::Abs(DX), FMath::Abs(DY));
		if (Horizontal == 0)
		{
			return E::Unproven;
		}
		const int64 Radius = int64(Capsule.RadiusCm) + Capsule.ClearanceMarginCm;
		if (Radius > Segment.RadiusCm)
		{
			return E::Unproven;
		}
		OutFloorSlack = CalculateSlack(Segment.Profile.StartFloorCm, Segment.Profile.EndFloorCm, Radius, Horizontal);
		OutCeilingSlack = CalculateSlack(Segment.Profile.StartCeilingCm, Segment.Profile.EndCeilingCm, Radius, Horizontal);
		return E::None;
	}
};

EVoxelLandM5ErrorCode VoxelLand::M5::ProfileContainment::Check(const FVoxelLandM5Segment& Segment,
	const FVoxelLandM5CapsuleProfile& Capsule, const FVoxelLandM5PointCm& Start, const FVoxelLandM5PointCm& End)
{
	using E = EVoxelLandM5ErrorCode;
	using I = FVoxelLandM5ProfileContainmentInternals;
	int64 FloorSlack = 0;
	int64 CeilingSlack = 0;
	const E BoundsError = I::CalculateBounds(Segment, Capsule, FloorSlack, CeilingSlack);
	if (BoundsError != E::None)
	{
		return BoundsError;
	}
	if (!I::PointInRange(Start) || !I::PointInRange(End))
	{
		return E::OutOfDomain;
	}
	const bool bForward = Start.X == Segment.StartCm.X && Start.Y == Segment.StartCm.Y
		&& End.X == Segment.EndCm.X && End.Y == Segment.EndCm.Y;
	const bool bReverse = Start.X == Segment.EndCm.X && Start.Y == Segment.EndCm.Y
		&& End.X == Segment.StartCm.X && End.Y == Segment.StartCm.Y;
	if (!bForward && !bReverse)
	{
		return E::Unproven;
	}
	const int64 Radius = int64(Capsule.RadiusCm) + Capsule.ClearanceMarginCm;
	const int64 Margin = Capsule.ClearanceMarginCm;
	const int64 Height = Capsule.StandingHeightCm;
	if (!I::EnvelopeInRange(Start, Radius, Margin, Height) || !I::EnvelopeInRange(End, Radius, Margin, Height))
	{
		return E::OutOfDomain;
	}
	const int64 Horizontal = FMath::Max(FMath::Abs(End.X - Start.X), FMath::Abs(End.Y - Start.Y));
	if (I::DegreesPerRightAngle * FMath::Abs(End.Z - Start.Z) > int64(Capsule.MaxSlopeDegrees) * Horizontal)
	{
		return E::Unproven;
	}
	const int64 FirstFloor = bForward ? Segment.Profile.StartFloorCm : Segment.Profile.EndFloorCm;
	const int64 LastFloor = bForward ? Segment.Profile.EndFloorCm : Segment.Profile.StartFloorCm;
	const int64 FirstCeiling = bForward ? Segment.Profile.StartCeilingCm : Segment.Profile.EndCeilingCm;
	const int64 LastCeiling = bForward ? Segment.Profile.EndCeilingCm : Segment.Profile.StartCeilingCm;
	if (Start.Z - Margin < FirstFloor + FloorSlack || End.Z - Margin < LastFloor + FloorSlack
		|| Start.Z + Height + Margin > FirstCeiling - CeilingSlack
		|| End.Z + Height + Margin > LastCeiling - CeilingSlack)
	{
		return E::Unproven;
	}
	return E::None;
}

EVoxelLandM5ErrorCode VoxelLand::M5::ProfileContainment::Propose(const FVoxelLandM5Segment& Segment,
	const FVoxelLandM5CapsuleProfile& Capsule, FVoxelLandM5FeetProposal& Out)
{
	using E = EVoxelLandM5ErrorCode;
	int64 FloorSlack = 0;
	int64 CeilingSlack = 0;
	const E BoundsError = FVoxelLandM5ProfileContainmentInternals::CalculateBounds(Segment, Capsule, FloorSlack, CeilingSlack);
	if (BoundsError != E::None)
	{
		return BoundsError;
	}
	FVoxelLandM5FeetProposal Scratch;
	Scratch.StartFeetCm = Segment.StartCm;
	Scratch.EndFeetCm = Segment.EndCm;
	Scratch.StartFeetCm.Z = Segment.Profile.StartFloorCm + Capsule.ClearanceMarginCm + FloorSlack;
	Scratch.EndFeetCm.Z = Segment.Profile.EndFloorCm + Capsule.ClearanceMarginCm + FloorSlack;
	const E CheckError = Check(Segment, Capsule, Scratch.StartFeetCm, Scratch.EndFeetCm);
	if (CheckError != E::None)
	{
		return CheckError;
	}
	Out = Scratch;
	return E::None;
}
