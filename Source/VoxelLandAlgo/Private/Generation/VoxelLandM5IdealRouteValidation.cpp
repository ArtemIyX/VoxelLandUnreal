#include "Generation/VoxelLandM5IdealRouteValidation.h"

#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5RouteTopology.h"
#include "Generation/VoxelLandM5SegmentCoverage.h"

namespace
{
FVoxelLandM5Diagnostic IdealRouteFailure(
	EVoxelLandM5ErrorCode Code, const TCHAR* Message, bool bHasFeatureId = false, uint64 FeatureId = 0)
{
	FVoxelLandM5Diagnostic Error;
	Error.Code = Code;
	Error.Stage = TEXT("M5.IdealRouteValidation");
	Error.Message = Message;
	Error.bHasFeatureId = bHasFeatureId;
	Error.FeatureId = FeatureId;
	return Error;
}
}

FVoxelLandM5Diagnostic VoxelLand::M5::IdealRouteValidation::Validate(const FVoxelLandM5CaveSettings& Settings,
	int64 WorldSeed, const FVoxelLandM5RegionCoord& Region,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	const FVoxelLandM5Diagnostic Structural =
		RouteTopology::ValidateStructure(Settings, WorldSeed, Region, Candidate, Budget, IsCancelled);
	if (Structural.Code != EVoxelLandM5ErrorCode::None)
	{
		return Structural;
	}
	const FVoxelLandM5CavePlan& Plan = Candidate->GetPlan();
	if (!Budget->TryChargeWork(int64(Plan.Routes.Num()) + 1))
	{
		return IdealRouteFailure(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Ideal route count work limit exceeded"));
	}
	int64 Legs = 0;
	for (const FVoxelLandM5Route& Route : Plan.Routes)
	{
		if (IsCancelled())
		{
			return IdealRouteFailure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Ideal route validation cancelled"));
		}
		if (!CheckedMath::TryAdd(Legs, Route.Legs.Num(), Legs))
		{
			return IdealRouteFailure(EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Route leg count overflow"));
		}
	}
	// Each pair is inspected twice, with a cancellation poll per inspection.
	// Also bound both route-loop traversals, loop control, segment handling and
	// the final cancellation poll. SegmentCoverage charges its own work.
	int64 PairWork = 0;
	int64 RouteScans = 0;
	int64 LegWork = 0;
	int64 SegmentWork = 0;
	int64 Work = 0;
	if (!CheckedMath::TryMultiplyNonNegative(Legs, Plan.Segments.Num(), PairWork)
		|| !CheckedMath::TryMultiplyNonNegative(PairWork, 4, PairWork)
		|| !CheckedMath::TryMultiplyNonNegative(Plan.Routes.Num(), Plan.Segments.Num(), RouteScans)
		|| !CheckedMath::TryMultiplyNonNegative(RouteScans, 2, RouteScans)
		|| !CheckedMath::TryMultiplyNonNegative(Legs, 4, LegWork)
		|| !CheckedMath::TryMultiplyNonNegative(Plan.Segments.Num(), 4, SegmentWork)
		|| !CheckedMath::TryAdd(PairWork, RouteScans, Work)
		|| !CheckedMath::TryAdd(Work, 2 * int64(Plan.Routes.Num()), Work) || !CheckedMath::TryAdd(Work, LegWork, Work)
		|| !CheckedMath::TryAdd(Work, SegmentWork, Work) || !CheckedMath::TryAdd(Work, 8, Work))
	{
		return IdealRouteFailure(EVoxelLandM5ErrorCode::ArithmeticOverflow, TEXT("Coverage work overflow"));
	}
	if (!Budget->TryChargeWork(Work))
	{
		return IdealRouteFailure(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Ideal route work limit exceeded"));
	}
	for (const FVoxelLandM5Route& Route : Plan.Routes)
	{
		for (const FVoxelLandM5RouteLeg& Leg : Route.Legs)
		{
			const FVoxelLandM5Segment* Match = nullptr;
			for (const FVoxelLandM5Segment& Segment : Plan.Segments)
			{
				if (IsCancelled())
				{
					return IdealRouteFailure(
						EVoxelLandM5ErrorCode::Cancelled, TEXT("Ideal route validation cancelled"));
				}
				if (Segment.bHasId && Segment.Id == Leg.Feature.Id)
				{
					if (Match)
					{
						return IdealRouteFailure(
							EVoxelLandM5ErrorCode::IdentityCollision, TEXT("Duplicate segment ID"), true, Segment.Id);
					}
					Match = &Segment;
				}
			}
			if (!Match)
			{
				return IdealRouteFailure(EVoxelLandM5ErrorCode::InconsistentTopology, TEXT("Leg segment is missing"),
					Leg.Feature.bValid, Leg.Feature.Id);
			}
			const FVoxelLandM5Diagnostic Covered =
				SegmentCoverage::Check(*Match, Leg, Region, Settings.Capsule, Budget, IsCancelled);
			if (Covered.Code != EVoxelLandM5ErrorCode::None)
			{
				return Covered;
			}
		}
	}
	for (const FVoxelLandM5Segment& Segment : Plan.Segments)
	{
		int64 Owners = 0;
		for (const FVoxelLandM5Route& Route : Plan.Routes)
		{
			for (const FVoxelLandM5RouteLeg& Leg : Route.Legs)
			{
				if (IsCancelled())
				{
					return IdealRouteFailure(
						EVoxelLandM5ErrorCode::Cancelled, TEXT("Ideal route validation cancelled"));
				}
				if (Leg.Feature.bValid && Leg.Feature.Kind == EVoxelLandM5RouteFeatureKind::Segment
					&& Leg.Feature.Region.X == Region.X && Leg.Feature.Region.Y == Region.Y
					&& Leg.Feature.Id == Segment.Id)
				{
					++Owners;
				}
			}
		}
		if (Owners != 1)
		{
			return IdealRouteFailure(EVoxelLandM5ErrorCode::InconsistentTopology,
				TEXT("Segment does not have exactly one traversing leg"), Segment.bHasId, Segment.Id);
		}
	}
	if (IsCancelled())
	{
		return IdealRouteFailure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Ideal route validation cancelled"));
	}
	return FVoxelLandM5Diagnostic();
}
