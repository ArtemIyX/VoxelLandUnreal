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

	static bool SegmentCoverageSamePoint(const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B)
	{
		return A.X == B.X && A.Y == B.Y && A.Z == B.Z;
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
	FVoxelLandM5FeetProposal Proposal;
	const FVoxelLandM5Diagnostic Error = FeetProposalBuilder::Build(Segment, Capsule, Budget, IsCancelled, Proposal);
	if (Error.Code != EVoxelLandM5ErrorCode::None)
	{
		return Error;
	}
	const bool bForward =
		FVoxelLandM5SegmentCoverageInternals::SegmentCoverageSamePoint(Leg.StartFeetCm, Proposal.StartFeetCm)
		&& FVoxelLandM5SegmentCoverageInternals::SegmentCoverageSamePoint(Leg.EndFeetCm, Proposal.EndFeetCm);
	const bool bReverse =
		FVoxelLandM5SegmentCoverageInternals::SegmentCoverageSamePoint(Leg.StartFeetCm, Proposal.EndFeetCm)
		&& FVoxelLandM5SegmentCoverageInternals::SegmentCoverageSamePoint(Leg.EndFeetCm, Proposal.StartFeetCm);
	if (!bForward && !bReverse)
	{
		return FVoxelLandM5SegmentCoverageInternals::SegmentCoverageFailure(EVoxelLandM5ErrorCode::Unproven,
			TEXT("Leg does not traverse the complete ideal segment proposal"), Segment);
	}
	if (IsCancelled())
	{
		return FVoxelLandM5SegmentCoverageInternals::SegmentCoverageFailure(
			EVoxelLandM5ErrorCode::Cancelled, TEXT("Segment coverage cancelled"), Segment);
	}
	return FVoxelLandM5Diagnostic();
}
