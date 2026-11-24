#include "Generation/VoxelLandM5SegmentCoverage.h"

#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5FeetProposalBuilder.h"

class FVoxelLandM5SegmentCoverageInternals final
{
public:
	static FVoxelLandM5Diagnostic SegmentCoverageFailure(
		EVoxelLandM5ErrorCode Code, const TCHAR* Message, const FVoxelLandM5Segment& Segment)
	{
		FVoxelLandM5Diagnostic Error;
		Error.Code = Code;
		Error.Stage = TEXT("M5.SegmentCoverage");
		Error.bHasFeatureId = Segment.bHasId;
		Error.FeatureId = Segment.Id;
		Error.Message = Message;
		return Error;
	}

	static constexpr int64 CoordinateLimit = 1048576;

	static bool SegmentCoverageSamePoint(const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B)
	{
		return A.X == B.X && A.Y == B.Y && A.Z == B.Z;
	}

	static bool InRange(int64 Value)
	{
		return Value >= -CoordinateLimit && Value <= CoordinateLimit;
	}

	static bool PointInRange(const FVoxelLandM5PointCm& Point)
	{
		return InRange(Point.X) && InRange(Point.Y) && InRange(Point.Z);
	}

	static bool EnvelopeInRange(const FVoxelLandM5PointCm& Feet, int64 Radius, int64 Height, int64 Margin)
	{
		return InRange(Feet.X - Radius - Margin) && InRange(Feet.X + Radius + Margin)
			&& InRange(Feet.Y - Radius - Margin) && InRange(Feet.Y + Radius + Margin)
			&& InRange(Feet.Z - Margin) && InRange(Feet.Z + Height + Margin);
	}

	static bool SameXY(const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B)
	{
		return A.X == B.X && A.Y == B.Y;
	}

	static bool CapsuleInsideEndpointBall(const FVoxelLandM5PointCm& Feet,
		const FVoxelLandM5PointCm& Center, int64 CapsuleRadius, int64 Height, int64 SegmentRadius)
	{
		const int64 LowerOffset = Feet.Z + CapsuleRadius - Center.Z;
		const int64 UpperOffset = Feet.Z + Height - CapsuleRadius - Center.Z;
		return FMath::Abs(LowerOffset) + CapsuleRadius <= SegmentRadius
			&& FMath::Abs(UpperOffset) + CapsuleRadius <= SegmentRadius;
	}
};

FVoxelLandM5Diagnostic VoxelLand::M5::SegmentCoverage::Check(const FVoxelLandM5Segment& Segment,
	const FVoxelLandM5RouteLeg& Leg, const FVoxelLandM5RegionCoord& Region, const FVoxelLandM5CapsuleProfile& Capsule,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	if (IsCancelled())
	{
		return FVoxelLandM5SegmentCoverageInternals::SegmentCoverageFailure(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Segment coverage cancelled"), Segment);
	}
	if (!Budget->TryChargeWork(8))
	{
		return FVoxelLandM5SegmentCoverageInternals::SegmentCoverageFailure(
			EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Segment coverage work limit exceeded"), Segment);
	}
	if (!Segment.bHasId || !Leg.Feature.bValid || Leg.Feature.Kind != EVoxelLandM5RouteFeatureKind::Segment
		|| Leg.Feature.Id != Segment.Id || Leg.Feature.Region.X != Region.X || Leg.Feature.Region.Y != Region.Y)
	{
		return FVoxelLandM5SegmentCoverageInternals::SegmentCoverageFailure(EVoxelLandM5ErrorCode::InconsistentTopology,
			TEXT("Leg does not identify the required region-qualified segment"), Segment);
	}
	if (Leg.Transition != EVoxelLandM5RouteTransition::Walk)
	{
		return FVoxelLandM5SegmentCoverageInternals::SegmentCoverageFailure(
			EVoxelLandM5ErrorCode::Unproven, TEXT("Segment transition is not a straight walk"), Segment);
	}
	using I = FVoxelLandM5SegmentCoverageInternals;
	const int64 Height = Capsule.StandingHeightCm;
	const int64 Radius = Capsule.RadiusCm;
	const int64 Margin = Capsule.ClearanceMarginCm;
	if (Segment.RadiusCm < 1 || Segment.RadiusCm > I::CoordinateLimit || Height < 1
		|| Height > I::CoordinateLimit || Radius < 1 || Radius > I::CoordinateLimit
		|| Margin < 0 || Margin > I::CoordinateLimit || Height < 2 * Radius
		|| Capsule.MaxSlopeDegrees < 0 || Capsule.MaxSlopeDegrees > 90 || Capsule.MaxStepCm < 0)
	{
		return I::SegmentCoverageFailure(EVoxelLandM5ErrorCode::InvalidInput,
			TEXT("Invalid segment or capsule profile"), Segment);
	}
	if (!I::PointInRange(Segment.StartCm) || !I::PointInRange(Segment.EndCm))
	{
		return I::SegmentCoverageFailure(EVoxelLandM5ErrorCode::OutOfDomain,
			TEXT("Segment lies outside the supported domain"), Segment);
	}
	if (I::SegmentCoverageSamePoint(Segment.StartCm, Segment.EndCm))
	{
		// Preserve the original validator's work, cancellation and diagnostic.
		FVoxelLandM5FeetProposal Unused;
		return FeetProposalBuilder::Build(Segment, Capsule, Budget, IsCancelled, Unused);
	}
	if (!I::PointInRange(Leg.StartFeetCm) || !I::PointInRange(Leg.EndFeetCm))
	{
		return I::SegmentCoverageFailure(EVoxelLandM5ErrorCode::OutOfDomain,
			TEXT("Feet lie outside the supported domain"), Segment);
	}
	const bool bForward = I::SameXY(Leg.StartFeetCm, Segment.StartCm) && I::SameXY(Leg.EndFeetCm, Segment.EndCm);
	const bool bReverse = I::SameXY(Leg.StartFeetCm, Segment.EndCm) && I::SameXY(Leg.EndFeetCm, Segment.StartCm);
	if (!bForward && !bReverse)
	{
		return I::SegmentCoverageFailure(EVoxelLandM5ErrorCode::Unproven,
			TEXT("Feet do not span the segment endpoint coordinates"), Segment);
	}
	const FVoxelLandM5PointCm& FirstCenter = bForward ? Segment.StartCm : Segment.EndCm;
	const FVoxelLandM5PointCm& LastCenter = bForward ? Segment.EndCm : Segment.StartCm;
	const bool bCentered = Leg.StartFeetCm.Z == FirstCenter.Z - Height / 2
		&& Leg.EndFeetCm.Z == LastCenter.Z - Height / 2;
	if (bCentered)
	{
		// Preserve the original stricter inflated-ball proof for canonical legs.
		FVoxelLandM5FeetProposal Proposal;
		const FVoxelLandM5Diagnostic Error = FeetProposalBuilder::Build(Segment, Capsule, Budget, IsCancelled, Proposal);
		if (Error.Code != EVoxelLandM5ErrorCode::None)
		{
			return Error;
		}
		const bool bCanonicalForward = I::SegmentCoverageSamePoint(Leg.StartFeetCm, Proposal.StartFeetCm)
			&& I::SegmentCoverageSamePoint(Leg.EndFeetCm, Proposal.EndFeetCm);
		const bool bCanonicalReverse = I::SegmentCoverageSamePoint(Leg.StartFeetCm, Proposal.EndFeetCm)
			&& I::SegmentCoverageSamePoint(Leg.EndFeetCm, Proposal.StartFeetCm);
		if (!bCanonicalForward && !bCanonicalReverse)
		{
			return I::SegmentCoverageFailure(EVoxelLandM5ErrorCode::Unproven,
				TEXT("Leg does not traverse the canonical segment proposal"), Segment);
		}
	}
	else
	{
		if (!Budget->TryChargeWork(32))
		{
			return I::SegmentCoverageFailure(EVoxelLandM5ErrorCode::BudgetExceeded,
				TEXT("Explicit feet work limit exceeded"), Segment);
		}
		const int64 DX = Leg.EndFeetCm.X - Leg.StartFeetCm.X;
		const int64 DY = Leg.EndFeetCm.Y - Leg.StartFeetCm.Y;
		const int64 DZ = Leg.EndFeetCm.Z - Leg.StartFeetCm.Z;
		const int64 HorizontalLowerBound = FMath::Max(FMath::Abs(DX), FMath::Abs(DY));
		// Without horizontal extent, matching XY cannot establish traversal of
		// the segment; any vertical transition needs a separate proof.
		if (HorizontalLowerBound == 0 || 90 * FMath::Abs(DZ) > int64(Capsule.MaxSlopeDegrees) * HorizontalLowerBound)
		{
			return I::SegmentCoverageFailure(EVoxelLandM5ErrorCode::Unproven,
				TEXT("Explicit feet path does not prove segment extent and slope"), Segment);
		}
		if (!I::EnvelopeInRange(Leg.StartFeetCm, Radius, Height, Margin)
			|| !I::EnvelopeInRange(Leg.EndFeetCm, Radius, Height, Margin))
		{
			return I::SegmentCoverageFailure(EVoxelLandM5ErrorCode::OutOfDomain,
				TEXT("Capsule envelope lies outside the supported domain"), Segment);
		}
		// At each endpoint both spherical end centers are on the segment's
		// XY centerline. Each nominal end sphere must fit the endpoint ball.
		// Convexity then contains every interpolated capsule in the swept tube.
		if (!I::CapsuleInsideEndpointBall(Leg.StartFeetCm, FirstCenter, Radius, Height, Segment.RadiusCm)
			|| !I::CapsuleInsideEndpointBall(Leg.EndFeetCm, LastCenter, Radius, Height, Segment.RadiusCm))
		{
			return I::SegmentCoverageFailure(EVoxelLandM5ErrorCode::Unproven,
				TEXT("Nominal capsule is not contained in the endpoint balls"), Segment);
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandM5SegmentCoverageInternals::SegmentCoverageFailure(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Segment coverage cancelled"), Segment);
	}
	return FVoxelLandM5Diagnostic();
}
