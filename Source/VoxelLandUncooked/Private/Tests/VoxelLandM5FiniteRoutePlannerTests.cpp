#if WITH_DEV_AUTOMATION_TESTS

#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5FiniteRoutePlanner.h"
#include "Misc/AutomationTest.h"

class FVoxelLandM5FiniteRoutePlannerFixtureFactory final
{
public:
	static TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Make(
		const FVoxelLandM5CaveSettings& Settings, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		FVoxelLandM5CavePlan Plan)
	{
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
		if (!Budget->TryReserveBytes(65536, Lease))
		{
			return nullptr;
		}
		const FVoxelLandM5RegionCoord Region = Plan.Region;
		return MakeShareable(new FVoxelLandM5CandidateResult(
			Budget, Settings, 11, Region, MoveTemp(Plan), MoveTemp(Lease)));
	}

	static FVoxelLandM5CavePlan Plan(const FVoxelLandM5CaveSettings& Settings)
	{
		FVoxelLandM5CavePlan Result;
		Result.Bounds = Settings.Bounds;
		FVoxelLandM5Chamber A;
		A.bHasId = true;
		A.Id = 1;
		A.CenterCm.X = -200;
		A.CenterCm.Z = 200;
		A.RadiusCm = 200;
		FVoxelLandM5Chamber B = A;
		B.Id = 2;
		B.CenterCm.X = 200;
		Result.Chambers = {A, B};
		FVoxelLandM5Entrance Entrance;
		Entrance.bHasId = true;
		Entrance.Id = 3;
		Entrance.bHasConnectionId = true;
		Entrance.ConnectionId = 8;
		Entrance.OpeningBoundsCm.MinInclusive.X = 250;
		Entrance.OpeningBoundsCm.MinInclusive.Y = -200;
		Entrance.OpeningBoundsCm.MinInclusive.Z = 100;
		Entrance.OpeningBoundsCm.MaxExclusive.X = 500;
		Entrance.OpeningBoundsCm.MaxExclusive.Y = 200;
		Entrance.OpeningBoundsCm.MaxExclusive.Z = 300;
		Result.Entrances.Add(Entrance);
		FVoxelLandM5Connection First;
		First.bHasId = true;
		First.Id = 7;
		First.Start.bValid = First.End.bValid = true;
		First.Start.Id = 1;
		First.End.Id = 2;
		FVoxelLandM5Connection Second = First;
		Second.Id = 8;
		Second.Start.Id = 2;
		Second.End.Kind = EVoxelLandM5EndpointKind::Entrance;
		Second.End.Id = 3;
		Result.Connections = {First, Second};
		for (int32 Index = 0; Index < 3; ++Index)
		{
			FVoxelLandM5Segment Segment;
			Segment.bHasId = Segment.bHasConnectionId = true;
			Segment.Id = uint64(9 + Index);
			Segment.ConnectionId = Index == 0 ? 7 : 8;
			Segment.StartCm.X = Index == 0 ? -200 : (Index == 1 ? 200 : 300);
			Segment.EndCm.X = Index == 0 ? 200 : (Index == 1 ? 300 : 400);
			Segment.StartCm.Z = Segment.EndCm.Z = 200;
			Segment.RadiusCm = 150;
			Result.Segments.Add(Segment);
		}
		return Result;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5FiniteRoutePlannerTest, "VoxelLand.M5.Routes.FiniteAutomatic",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5FiniteRoutePlannerTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings Settings;
	Settings.bEnabled = true;
	Settings.StageId = FGuid(1, 2, 3, 4);
	Settings.Topology.MinChambers = Settings.Topology.MaxChambers = 2;
	Settings.Topology.EntranceCount = 1;
	Settings.Topology.MinLoops = Settings.Topology.MaxLoops = 0;
	Settings.Capsule.ClearanceMarginCm = 0;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!FVoxelLandM5Budget::TryCreate(Settings.Limits.MaxBytes, Settings.Limits.MaxValidationWork, Budget))
	{
		return false;
	}
	const FVoxelLandM5CavePlan Plan = FVoxelLandM5FiniteRoutePlannerFixtureFactory::Plan(Settings);
	const auto Candidate = FVoxelLandM5FiniteRoutePlannerFixtureFactory::Make(Settings, Budget.ToSharedRef(), Plan);
	if (!Candidate)
	{
		return false;
	}
	int32 Calls = 0;
	const auto Never = [] { return false; };
	const auto Resolve = [&Calls](const FVoxelLandM5PointCm& Center,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> SharedBudget, TFunctionRef<bool()> Cancel,
		FVoxelLandM5PointCm& Feet) {
		FVoxelLandM5Diagnostic Result;
		if (Cancel())
		{
			Result.Code = EVoxelLandM5ErrorCode::Cancelled;
			return Result;
		}
		if (!SharedBudget->TryChargeWork(1))
		{
			Result.Code = EVoxelLandM5ErrorCode::BudgetExceeded;
			return Result;
		}
		++Calls;
		Feet = Center;
		Feet.Z -= 100;
		return Result;
	};
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Out;
	const FVoxelLandM5RegionCoord Region;
	const auto Initial = VoxelLand::M5::FiniteRoutePlanner::Build(
		Settings, 11, Region, Candidate.ToSharedRef(), Budget.ToSharedRef(), Resolve, Never, Out);
	if (!TestTrue(TEXT("complete finite routes"), Initial.Code == EVoxelLandM5ErrorCode::None && Out.IsValid()))
	{
		return false;
	}
	if (!TestEqual(TEXT("every connection has a route"), Out->GetPlan().Routes.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("shared center resolved once"), Calls, 4);
	TestEqual(TEXT("stable first connection ID"), Out->GetPlan().Routes[0].Id, uint64(7));
	TestEqual(TEXT("stable second connection ID"), Out->GetPlan().Routes[1].Id, uint64(8));
	TestEqual(TEXT("entrance route has two legs"), Out->GetPlan().Routes[1].Legs.Num(), 2);
	TestTrue(TEXT("entrance has both anchors"), Out->GetPlan().Entrances[0].bHasInsideFeetAnchor
		&& Out->GetPlan().Entrances[0].bHasOutsideFeetAnchor);
	TestEqual(TEXT("inside uses penultimate feet"), Out->GetPlan().Entrances[0].InsideFeetAnchorCm.X, int64(300));
	TestEqual(TEXT("outside uses terminal feet"), Out->GetPlan().Entrances[0].OutsideFeetAnchorCm.X, int64(400));
	TestEqual(TEXT("resolver supplies height"), Out->GetPlan().Entrances[0].OutsideFeetAnchorCm.Z, int64(100));

	FVoxelLandM5CavePlan Reordered = Plan;
	Swap(Reordered.Connections[0], Reordered.Connections[1]);
	Swap(Reordered.Segments[0], Reordered.Segments[2]);
	const auto Permuted = FVoxelLandM5FiniteRoutePlannerFixtureFactory::Make(Settings, Budget.ToSharedRef(), Reordered);
	if (!Permuted)
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> ReorderedOut;
	Calls = 0;
	const auto Again = VoxelLand::M5::FiniteRoutePlanner::Build(
		Settings, 11, Region, Permuted.ToSharedRef(), Budget.ToSharedRef(), Resolve, Never, ReorderedOut);
	TestTrue(TEXT("permuted source resolves"), Again.Code == EVoxelLandM5ErrorCode::None && ReorderedOut.IsValid());
	if (ReorderedOut && ReorderedOut->GetPlan().Routes.Num() == 2)
	{
		TestEqual(TEXT("canonical connection ordering"), ReorderedOut->GetPlan().Routes[0].Id, uint64(7));
		TestEqual(TEXT("canonical final segment"), ReorderedOut->GetPlan().Routes[1].Legs.Last().Feature.Id, uint64(11));
	}
	TestEqual(TEXT("permuted source resolves shared point once"), Calls, 4);

	const auto Prior = Out;
	FVoxelLandM5CavePlan Branched = Plan;
	FVoxelLandM5Segment Extra = Branched.Segments[1];
	Extra.Id = 12;
	Extra.EndCm.Y = 100;
	Branched.Segments.Add(Extra);
	const auto BranchCandidate = FVoxelLandM5FiniteRoutePlannerFixtureFactory::Make(Settings, Budget.ToSharedRef(), Branched);
	if (!BranchCandidate)
	{
		return false;
	}
	const auto Branch = VoxelLand::M5::FiniteRoutePlanner::Build(
		Settings, 11, Region, BranchCandidate.ToSharedRef(), Budget.ToSharedRef(), Resolve, Never, Out);
	TestTrue(TEXT("branch rejected atomically"), Branch.Code == EVoxelLandM5ErrorCode::InconsistentTopology && Out == Prior);
	FVoxelLandM5CavePlan Missing = Plan;
	Missing.Segments.RemoveAt(1);
	const auto MissingCandidate = FVoxelLandM5FiniteRoutePlannerFixtureFactory::Make(Settings, Budget.ToSharedRef(), Missing);
	if (!MissingCandidate)
	{
		return false;
	}
	const auto MissingResult = VoxelLand::M5::FiniteRoutePlanner::Build(
		Settings, 11, Region, MissingCandidate.ToSharedRef(), Budget.ToSharedRef(), Resolve, Never, Out);
	TestTrue(TEXT("disconnected chain rejected atomically"), MissingResult.Code != EVoxelLandM5ErrorCode::None && Out == Prior);

	const auto FailResolve = [](const FVoxelLandM5PointCm&, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		TFunctionRef<bool()>, FVoxelLandM5PointCm&) {
		FVoxelLandM5Diagnostic Result;
		Result.Code = EVoxelLandM5ErrorCode::Unproven;
		return Result;
	};
	const auto Failed = VoxelLand::M5::FiniteRoutePlanner::Build(
		Settings, 11, Region, Candidate.ToSharedRef(), Budget.ToSharedRef(), FailResolve, Never, Out);
	TestTrue(TEXT("resolver failure preserves output"), Failed.Code == EVoxelLandM5ErrorCode::Unproven && Out == Prior);
	const auto Cancelled = VoxelLand::M5::FiniteRoutePlanner::Build(
		Settings, 11, Region, Candidate.ToSharedRef(), Budget.ToSharedRef(), Resolve, [] { return true; }, Out);
	TestTrue(TEXT("cancellation preserves output"), Cancelled.Code == EVoxelLandM5ErrorCode::Cancelled && Out == Prior);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> EmptyWork;
	if (!FVoxelLandM5Budget::TryCreate(Settings.Limits.MaxBytes, 1, EmptyWork))
	{
		return false;
	}
	const auto LowCandidate = FVoxelLandM5FiniteRoutePlannerFixtureFactory::Make(Settings, EmptyWork.ToSharedRef(), Plan);
	if (!LowCandidate)
	{
		return false;
	}
	const auto Low = VoxelLand::M5::FiniteRoutePlanner::Build(
		Settings, 11, Region, LowCandidate.ToSharedRef(), EmptyWork.ToSharedRef(), Resolve, Never, Out);
	TestTrue(TEXT("work exhaustion preserves output"), Low.Code == EVoxelLandM5ErrorCode::BudgetExceeded && Out == Prior);
	return true;
}

#endif
