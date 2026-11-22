#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5EntranceMask.h"
#include "Generation/VoxelLandM5Seed.h"
#include "Data/Enums/VoxelLandM5PortalAxis.h"
#include "Misc/AutomationTest.h"

class FVoxelLandM5EntranceMaskFixtureFactory
{
public:
	static TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Make(const FVoxelLandM5CavePlan& Plan,
		const FVoxelLandM5CaveSettings& Settings, int64 Seed,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget)
	{
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
		if (!Budget->TryReserveBytes(16384, Lease))
		{
			return nullptr;
		}
		FVoxelLandM5CavePlan Copy = Plan;
		return MakeShareable(
			new FVoxelLandM5CandidateResult(Budget, Settings, Seed, Plan.Region, MoveTemp(Copy), MoveTemp(Lease)));
	}
};

namespace
{
constexpr EAutomationTestFlags M5EntranceMaskFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandM5PointCm M5EntranceMaskTestPoint(int64 X, int64 Y, int64 Z)
{
	FVoxelLandM5PointCm Point;
	Point.X = X;
	Point.Y = Y;
	Point.Z = Z;
	return Point;
}

FVoxelLandM5CaveSettings M5EntranceMaskTestSettings()
{
	FVoxelLandM5CaveSettings Settings;
	Settings.bEnabled = true;
	Settings.StageId = FGuid(1, 2, 3, 4);
	Settings.Topology.MinChambers = 1;
	Settings.Topology.MaxChambers = 1;
	Settings.Topology.EntranceCount = 1;
	Settings.Topology.MinLoops = 0;
	Settings.Topology.MaxLoops = 0;
	Settings.Bounds.MinInclusive = M5EntranceMaskTestPoint(-1000, -1000, -1000);
	Settings.Bounds.MaxExclusive = M5EntranceMaskTestPoint(1001, 1001, 1001);
	return Settings;
}

FVoxelLandM5CavePlan M5EntranceMaskTestPlan(const FVoxelLandM5CaveSettings& Settings)
{
	FVoxelLandM5CavePlan Plan;
	Plan.Bounds = Settings.Bounds;
	FVoxelLandM5Chamber Chamber;
	Chamber.bHasId = true;
	Chamber.Id = 3;
	Plan.Chambers.Add(Chamber);
	FVoxelLandM5Entrance Entrance;
	Entrance.bHasId = true;
	Entrance.Id = 4;
	Entrance.bHasConnectionId = true;
	Entrance.ConnectionId = 5;
	Entrance.OpeningBoundsCm.MinInclusive = M5EntranceMaskTestPoint(-250, -250, -250);
	Entrance.OpeningBoundsCm.MaxExclusive = M5EntranceMaskTestPoint(251, 251, 251);
	Plan.Entrances.Add(Entrance);
	FVoxelLandM5Connection Connection;
	Connection.bHasId = true;
	Connection.Id = 5;
	Connection.Start.bValid = true;
	Connection.Start.Kind = EVoxelLandM5EndpointKind::Chamber;
	Connection.Start.Id = 3;
	Connection.End.bValid = true;
	Connection.End.Kind = EVoxelLandM5EndpointKind::Entrance;
	Connection.End.Id = 4;
	Plan.Connections.Add(Connection);
	return Plan;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5EntranceMaskBoundaryTest, "VoxelLand.M5.EntranceMask.BoundaryAndSeams", M5EntranceMaskFlags)
bool FVoxelLandM5EntranceMaskBoundaryTest::RunTest(const FString& Parameters)
{
	const auto Never = [] {
		return false;
	};
	const FVoxelLandM5CaveSettings Settings = M5EntranceMaskTestSettings();
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget"), FVoxelLandM5Budget::TryCreate(1000000, 1000000, Budget)))
	{
		return false;
	}
	TArray<TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>> Plans;
	Plans.Add(FVoxelLandM5EntranceMaskFixtureFactory::Make(
		M5EntranceMaskTestPlan(Settings), Settings, 42, Budget.ToSharedRef()));
	if (!TestTrue(TEXT("candidate"), Plans[0].IsValid()))
	{
		return false;
	}
	TArray<uint8> Protection = {0, 0, 0};
	TSharedPtr<const FVoxelLandM5EntranceMask, ESPMode::ThreadSafe> Mask;
	FVoxelLandM5Diagnostic D = FVoxelLandM5EntranceMask::Build(FIntVector(9, 9, 9),
		M5EntranceMaskTestPoint(-400, -400, -400), 100, FIntVector(3, 4, 4), FIntVector(3, 1, 1), Protection, Settings,
		42, Plans, Budget.ToSharedRef(), Never, Mask);
	if (!TestEqual(TEXT("mask built"), D.Code, EVoxelLandM5ErrorCode::None) || !TestTrue(TEXT("mask"), Mask.IsValid()))
	{
		return false;
	}
	// A +/-200 cm influence fits opening [-250,251) only at x=0.
	TestEqual(TEXT("negative sample outside"), Mask->GetAuthorized()[0], uint8(0));
	TestEqual(TEXT("center fits"), Mask->GetAuthorized()[1], uint8(1));
	TestEqual(TEXT("positive sample outside"), Mask->GetAuthorized()[2], uint8(0));
	Protection[1] = 1;
	D = FVoxelLandM5EntranceMask::Build(FIntVector(9, 9, 9), M5EntranceMaskTestPoint(-400, -400, -400), 100,
		FIntVector(3, 4, 4), FIntVector(3, 1, 1), Protection, Settings, 42, Plans, Budget.ToSharedRef(), Never, Mask);
	if (!TestEqual(TEXT("protected valid"), D.Code, EVoxelLandM5ErrorCode::None))
	{
		return false;
	}
	TestEqual(TEXT("protection takes precedence"), Mask->GetAuthorized()[1], uint8(0));
	Protection[1] = 0;
	TSharedPtr<const FVoxelLandM5EntranceMask, ESPMode::ThreadSafe> Other;
	D = FVoxelLandM5EntranceMask::Build(FIntVector(9, 9, 9), M5EntranceMaskTestPoint(-500, -500, -500), 100,
		FIntVector(4, 5, 5), FIntVector(3, 1, 1), Protection, Settings, 42, Plans, Budget.ToSharedRef(), Never, Other);
	if (!TestEqual(TEXT("shifted window"), D.Code, EVoxelLandM5ErrorCode::None) || !Other.IsValid())
	{
		return false;
	}
	TestEqual(TEXT("same global negative sample"), Other->GetAuthorized()[0], uint8(0));
	TestEqual(TEXT("same global zero sample"), Other->GetAuthorized()[1], uint8(1));
	// Half-open upper bounds must include the entire closed influence interval.
	FVoxelLandM5CavePlan ExactPlan = M5EntranceMaskTestPlan(Settings);
	ExactPlan.Entrances[0].OpeningBoundsCm.MinInclusive = M5EntranceMaskTestPoint(-200, -200, -200);
	ExactPlan.Entrances[0].OpeningBoundsCm.MaxExclusive = M5EntranceMaskTestPoint(201, 201, 201);
	TArray<TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>> ExactPlans;
	ExactPlans.Add(FVoxelLandM5EntranceMaskFixtureFactory::Make(ExactPlan, Settings, 42, Budget.ToSharedRef()));
	TArray<uint8> OneProtection = {0};
	TSharedPtr<const FVoxelLandM5EntranceMask, ESPMode::ThreadSafe> Exact;
	D = FVoxelLandM5EntranceMask::Build(FIntVector(9, 9, 9), M5EntranceMaskTestPoint(-400, -400, -400), 100,
		FIntVector(4, 4, 4), FIntVector(1, 1, 1), OneProtection, Settings, 42, ExactPlans, Budget.ToSharedRef(), Never,
		Exact);
	if (!TestEqual(TEXT("exact boundary build"), D.Code, EVoxelLandM5ErrorCode::None) || !Exact.IsValid())
	{
		return false;
	}
	TestEqual(TEXT("closed influence fits at min and one below max"), Exact->GetAuthorized()[0], uint8(1));
	ExactPlan.Entrances[0].OpeningBoundsCm.MaxExclusive.X = 200;
	ExactPlans[0] = FVoxelLandM5EntranceMaskFixtureFactory::Make(ExactPlan, Settings, 42, Budget.ToSharedRef());
	D = FVoxelLandM5EntranceMask::Build(FIntVector(9, 9, 9), M5EntranceMaskTestPoint(-400, -400, -400), 100,
		FIntVector(4, 4, 4), FIntVector(1, 1, 1), OneProtection, Settings, 42, ExactPlans, Budget.ToSharedRef(), Never,
		Exact);
	if (!TestEqual(TEXT("exclusive boundary build"), D.Code, EVoxelLandM5ErrorCode::None) || !Exact.IsValid())
	{
		return false;
	}
	TestEqual(TEXT("influence touching exclusive max is blocked"), Exact->GetAuthorized()[0], uint8(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5EntranceMaskFailureTest, "VoxelLand.M5.EntranceMask.Failures", M5EntranceMaskFlags)
bool FVoxelLandM5EntranceMaskFailureTest::RunTest(const FString& Parameters)
{
	const auto Never = [] {
		return false;
	};
	const auto Always = [] {
		return true;
	};
	const FVoxelLandM5CaveSettings Settings = M5EntranceMaskTestSettings();
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget"), FVoxelLandM5Budget::TryCreate(1000000, 1000000, Budget)))
	{
		return false;
	}
	TArray<TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>> Plans;
	Plans.Add(FVoxelLandM5EntranceMaskFixtureFactory::Make(
		M5EntranceMaskTestPlan(Settings), Settings, 42, Budget.ToSharedRef()));
	TArray<uint8> Protection = {0};
	TSharedPtr<const FVoxelLandM5EntranceMask, ESPMode::ThreadSafe> Mask;
	const FIntVector Size(9, 9, 9), Start(4, 4, 4), Count(1, 1, 1);
	const auto Origin = M5EntranceMaskTestPoint(-400, -400, -400);
	FVoxelLandM5Diagnostic D = FVoxelLandM5EntranceMask::Build(
		Size, Origin, 100, Start, Count, Protection, Settings, 42, Plans, Budget.ToSharedRef(), Never, Mask);
	if (!TestEqual(TEXT("baseline valid"), D.Code, EVoxelLandM5ErrorCode::None) || !Mask.IsValid())
	{
		return false;
	}
	const auto Previous = Mask;
	D = FVoxelLandM5EntranceMask::Build(
		Size, Origin, 100, Start, Count, Protection, Settings, 42, Plans, Budget.ToSharedRef(), Always, Mask);
	TestEqual(TEXT("cancelled"), D.Code, EVoxelLandM5ErrorCode::Cancelled);
	TestTrue(TEXT("cancel preserves output"), Mask == Previous);
	D = FVoxelLandM5EntranceMask::Build(Size, Origin, 100, FIntVector(1, 4, 4), Count, Protection, Settings, 42, Plans,
		Budget.ToSharedRef(), Never, Mask);
	TestEqual(TEXT("missing halo"), D.Code, EVoxelLandM5ErrorCode::OutOfDomain);
	TestTrue(TEXT("halo failure preserves output"), Mask == Previous);
	D = FVoxelLandM5EntranceMask::Build(Size, M5EntranceMaskTestPoint(MAX_int64, 0, 0), 100, Start, Count, Protection,
		Settings, 42, Plans, Budget.ToSharedRef(), Never, Mask);
	TestEqual(TEXT("out of coordinate range"), D.Code, EVoxelLandM5ErrorCode::OutOfDomain);
	TestTrue(TEXT("range failure preserves output"), Mask == Previous);
	Protection[0] = 2;
	D = FVoxelLandM5EntranceMask::Build(
		Size, Origin, 100, Start, Count, Protection, Settings, 42, Plans, Budget.ToSharedRef(), Never, Mask);
	TestEqual(TEXT("invalid protection"), D.Code, EVoxelLandM5ErrorCode::InvalidInput);
	TestTrue(TEXT("invalid mask preserves output"), Mask == Previous);
	Protection[0] = 0;
	D = FVoxelLandM5EntranceMask::Build(
		Size, Origin, 100, Start, Count, Protection, Settings, 43, Plans, Budget.ToSharedRef(), Never, Mask);
	TestEqual(TEXT("wrong source seed"), D.Code, EVoxelLandM5ErrorCode::InvalidInput);
	TestTrue(TEXT("source failure preserves output"), Mask == Previous);
	int64 Live = 0, Peak = 0, Work = 0;
	Budget->GetUsage(Live, Peak, Work);
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> BlockingLease;
	if (!TestTrue(
			TEXT("reserve nearly all remaining memory"), Budget->TryReserveBytes(1000000 - Live - 1000, BlockingLease)))
	{
		return false;
	}
	D = FVoxelLandM5EntranceMask::Build(
		Size, Origin, 100, Start, Count, Protection, Settings, 42, Plans, Budget.ToSharedRef(), Never, Mask);
	TestEqual(TEXT("memory budget failure"), D.Code, EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("budget failure preserves output"), Mask == Previous);
	BlockingLease.Reset();
	FVoxelLandM5CaveSettings OverlapSettings = Settings;
	OverlapSettings.Topology.EntranceCount = 2;
	FVoxelLandM5CavePlan Overlap = M5EntranceMaskTestPlan(OverlapSettings);
	FVoxelLandM5Entrance Second = Overlap.Entrances[0];
	Second.Id = 6;
	Second.ConnectionId = 7;
	Overlap.Entrances.Add(Second);
	FVoxelLandM5Connection SecondLink = Overlap.Connections[0];
	SecondLink.Id = 7;
	SecondLink.End.Id = 6;
	Overlap.Connections.Add(SecondLink);
	Plans[0] = FVoxelLandM5EntranceMaskFixtureFactory::Make(Overlap, OverlapSettings, 42, Budget.ToSharedRef());
	D = FVoxelLandM5EntranceMask::Build(
		Size, Origin, 100, Start, Count, Protection, OverlapSettings, 42, Plans, Budget.ToSharedRef(), Never, Mask);
	TestEqual(TEXT("overlapping openings rejected"), D.Code, EVoxelLandM5ErrorCode::Unproven);
	TestTrue(TEXT("ambiguous openings preserve output"), Mask == Previous);
	Plans.Reset();
	D = FVoxelLandM5EntranceMask::Build(
		Size, Origin, 100, Start, Count, Protection, Settings, 42, Plans, Budget.ToSharedRef(), Never, Mask);
	TestEqual(TEXT("missing plan"), D.Code, EVoxelLandM5ErrorCode::InvalidInput);
	TestTrue(TEXT("missing plan preserves output"), Mask == Previous);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5EntranceMaskAdmissionTest, "VoxelLand.M5.EntranceMask.WorkAdmission", M5EntranceMaskFlags)
bool FVoxelLandM5EntranceMaskAdmissionTest::RunTest(const FString& Parameters)
{
	const auto Never = [] {
		return false;
	};
	const FVoxelLandM5CaveSettings Settings = M5EntranceMaskTestSettings();
	const FVoxelLandM5CavePlan Plan = M5EntranceMaskTestPlan(Settings);
	const FIntVector Size(9, 9, 9), Start(4, 4, 4), Count(1, 1, 1);
	const auto Origin = M5EntranceMaskTestPoint(-400, -400, -400);
	const TArray<uint8> Protection = {0};
	// One candidate preflight: 16*1^2 + 64*1 + 64 = 144.
	// One opening/sample: 16*1^2 + 16*(1+1)*1 + 256 + 64 = 368.
	// Structural topology has two nodes, one edge: 16*(2+1+1)^2 + 4096 = 4352.
	constexpr int64 ExactWork = 144 + 368 + 4352;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> ExactBudget;
	if (!TestTrue(TEXT("exact budget"), FVoxelLandM5Budget::TryCreate(100000, ExactWork, ExactBudget)))
	{
		return false;
	}
	TArray<TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>> Plans;
	Plans.Add(FVoxelLandM5EntranceMaskFixtureFactory::Make(Plan, Settings, 42, ExactBudget.ToSharedRef()));
	TSharedPtr<const FVoxelLandM5EntranceMask, ESPMode::ThreadSafe> Result;
	FVoxelLandM5Diagnostic D = FVoxelLandM5EntranceMask::Build(
		Size, Origin, 100, Start, Count, Protection, Settings, 42, Plans, ExactBudget.ToSharedRef(), Never, Result);
	if (!TestEqual(TEXT("exact succeeds"), D.Code, EVoxelLandM5ErrorCode::None) || !Result.IsValid())
	{
		return false;
	}
	int64 Live = 0, Peak = 0, Work = 0;
	ExactBudget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("exact cumulative charge"), Work, ExactWork);
	const auto Previous = Result;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> BelowBudget;
	if (!TestTrue(TEXT("below budget"), FVoxelLandM5Budget::TryCreate(100000, ExactWork - 1, BelowBudget)))
	{
		return false;
	}
	Plans[0] = FVoxelLandM5EntranceMaskFixtureFactory::Make(Plan, Settings, 42, BelowBudget.ToSharedRef());
	D = FVoxelLandM5EntranceMask::Build(
		Size, Origin, 100, Start, Count, Protection, Settings, 42, Plans, BelowBudget.ToSharedRef(), Never, Result);
	TestEqual(TEXT("one below fails"), D.Code, EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("one below preserves output"), Result == Previous);
	BelowBudget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("failed topology work is not charged"), Work, int64(144 + 368));
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> TinyBudget;
	if (!TestTrue(TEXT("tiny budget"), FVoxelLandM5Budget::TryCreate(100000, 143, TinyBudget)))
	{
		return false;
	}
	Plans[0] = FVoxelLandM5EntranceMaskFixtureFactory::Make(Plan, Settings, 42, TinyBudget.ToSharedRef());
	D = FVoxelLandM5EntranceMask::Build(
		Size, Origin, 100, Start, Count, Protection, Settings, 42, Plans, TinyBudget.ToSharedRef(), Never, Result);
	TestEqual(TEXT("preflight fails before candidate scan"), D.Code, EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("preflight preserves output"), Result == Previous);
	TinyBudget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("failed preflight adds no work"), Work, int64(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5EntranceMaskRegionalTest, "VoxelLand.M5.EntranceMask.RegionalAndCancellation", M5EntranceMaskFlags)
bool FVoxelLandM5EntranceMaskRegionalTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings Settings = M5EntranceMaskTestSettings();
	Settings.PlanningMode = EVoxelLandM5PlanningMode::Regional;
	Settings.Topology.MinChambers = 1;
	Settings.Topology.MaxChambers = 1;
	Settings.Topology.EntranceCount = 1;
	Settings.Topology.MinLoops = 0;
	Settings.Topology.MaxLoops = 0;
	Settings.Bounds.MinInclusive = M5EntranceMaskTestPoint(-204800, 0, -1000);
	Settings.Bounds.MaxExclusive = M5EntranceMaskTestPoint(204800, 204800, 1001);
	FVoxelLandM5CavePlan Left = M5EntranceMaskTestPlan(Settings);
	Left.Region.X = -1;
	Left.Bounds.MinInclusive = M5EntranceMaskTestPoint(-204800, 0, -1000);
	Left.Bounds.MaxExclusive = M5EntranceMaskTestPoint(0, 204800, 1001);
	Left.Connections[0].Start.Region = Left.Region;
	Left.Connections[0].End.Region = Left.Region;
	Left.Entrances[0].OpeningBoundsCm.MinInclusive = M5EntranceMaskTestPoint(-650, 750, -250);
	Left.Entrances[0].OpeningBoundsCm.MaxExclusive = M5EntranceMaskTestPoint(-149, 1251, 251);
	FVoxelLandM5CavePlan Right = M5EntranceMaskTestPlan(Settings);
	Right.Bounds.MinInclusive = M5EntranceMaskTestPoint(0, 0, -1000);
	Right.Bounds.MaxExclusive = M5EntranceMaskTestPoint(204800, 204800, 1001);
	Right.Entrances[0].OpeningBoundsCm.MinInclusive = M5EntranceMaskTestPoint(150, 750, -250);
	Right.Entrances[0].OpeningBoundsCm.MaxExclusive = M5EntranceMaskTestPoint(651, 1251, 251);
	// Canonical shared portal with an independently constructed connected graph
	// on each side. Topology validation verifies the full key and digest.
	FVoxelLandM5Portal Portal;
	Portal.bHasId = true;
	Portal.Key.WorldSeed = 42;
	Portal.Key.StageId = Settings.StageId;
	Portal.Key.Salt = Settings.Salt;
	Portal.Key.RegionA = Left.Region;
	Portal.Key.RegionB = Right.Region;
	Portal.Key.Axis = EVoxelLandM5PortalAxis::X;
	Portal.CenterCm = M5EntranceMaskTestPoint(0, 1000, 0);
	Portal.RadiusCm = 250;
	TArray<uint8> Digest;
	if (!TestTrue(TEXT("canonical portal digest"), VoxelLand::M5::Seed::TryPortalDigest(42, Settings.StageId,
													   static_cast<uint64>(Settings.Salt), -1, 0, 0, 0, Digest))
		|| !TestTrue(TEXT("portal id"), VoxelLand::M5::Seed::TryDigestWord(Digest, Portal.Id)))
	{
		return false;
	}
	Left.Portals.Add(Portal);
	Right.Portals.Add(Portal);
	FVoxelLandM5Connection PortalConnection;
	PortalConnection.bHasId = true;
	PortalConnection.Id = 8;
	PortalConnection.Start.bValid = true;
	PortalConnection.Start.Kind = EVoxelLandM5EndpointKind::Chamber;
	PortalConnection.Start.Id = 3;
	PortalConnection.End.bValid = true;
	PortalConnection.End.Kind = EVoxelLandM5EndpointKind::Portal;
	PortalConnection.End.Id = Portal.Id;
	PortalConnection.Start.Region = Left.Region;
	PortalConnection.End.Region = Left.Region;
	Left.Connections.Add(PortalConnection);
	PortalConnection.Start.Region = Right.Region;
	PortalConnection.End.Region = Right.Region;
	Right.Connections.Add(PortalConnection);
	Left.Chambers[0].CenterCm = M5EntranceMaskTestPoint(-1000, 1000, -500);
	Right.Chambers[0].CenterCm = M5EntranceMaskTestPoint(1000, 1000, -500);
	Left.Chambers[0].RadiusCm = Right.Chambers[0].RadiusCm = 500;
	FVoxelLandM5Segment Segment;
	Segment.bHasId = true;
	Segment.bHasConnectionId = true;
	Segment.Id = 10;
	Segment.ConnectionId = 5;
	Segment.RadiusCm = 250;
	Segment.StartCm = Left.Chambers[0].CenterCm;
	Segment.EndCm = M5EntranceMaskTestPoint(-400, 1000, 0);
	Left.Segments.Add(Segment);
	Segment.Id = 11;
	Segment.ConnectionId = 8;
	Segment.EndCm = Portal.CenterCm;
	Left.Segments.Add(Segment);
	Segment.Id = 10;
	Segment.ConnectionId = 5;
	Segment.StartCm = Right.Chambers[0].CenterCm;
	Segment.EndCm = M5EntranceMaskTestPoint(400, 1000, 0);
	Right.Segments.Add(Segment);
	Segment.Id = 11;
	Segment.ConnectionId = 8;
	Segment.EndCm = Portal.CenterCm;
	Right.Segments.Add(Segment);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("regional budget"), FVoxelLandM5Budget::TryCreate(1000000, 1000000, Budget)))
	{
		return false;
	}
	TArray<TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>> Plans;
	Plans.Add(FVoxelLandM5EntranceMaskFixtureFactory::Make(Left, Settings, 42, Budget.ToSharedRef()));
	Plans.Add(FVoxelLandM5EntranceMaskFixtureFactory::Make(Right, Settings, 42, Budget.ToSharedRef()));
	TArray<uint8> Protection;
	Protection.Init(0, 13);
	const FIntVector Size(23, 9, 9), Start(5, 4, 4), Count(13, 1, 1);
	const auto Origin = M5EntranceMaskTestPoint(-1100, 600, -400);
	TSharedPtr<const FVoxelLandM5EntranceMask, ESPMode::ThreadSafe> Result;
	int32 Polls = 0;
	const auto CountPolls = [&Polls] {
		++Polls;
		return false;
	};
	FVoxelLandM5Diagnostic D = FVoxelLandM5EntranceMask::Build(
		Size, Origin, 100, Start, Count, Protection, Settings, 42, Plans, Budget.ToSharedRef(), CountPolls, Result);
	if (!TestEqual(TEXT("both regional plans"), D.Code, EVoxelLandM5ErrorCode::None) || !Result.IsValid())
	{
		return false;
	}
	TestEqual(TEXT("negative region opening"), Result->GetAuthorized()[2], uint8(1));
	TestEqual(TEXT("between openings closed"), Result->GetAuthorized()[6], uint8(0));
	TestEqual(TEXT("positive region opening"), Result->GetAuthorized()[10], uint8(1));
	const auto Previous = Result;
	const int32 CancelAt = Polls - 2; // During the final output sample's opening scan.
	int32 CancelPolls = 0;
	const auto CancelDuringScan = [&CancelPolls, CancelAt] {
		return ++CancelPolls == CancelAt;
	};
	D = FVoxelLandM5EntranceMask::Build(Size, Origin, 100, Start, Count, Protection, Settings, 42, Plans,
		Budget.ToSharedRef(), CancelDuringScan, Result);
	TestEqual(TEXT("cancel after partial output scan"), D.Code, EVoxelLandM5ErrorCode::Cancelled);
	TestEqual(TEXT("reached deterministic poll"), CancelPolls, CancelAt);
	TestTrue(TEXT("cancel retains output"), Result == Previous);
	const auto Never = [] {
		return false;
	};
	FVoxelLandM5CavePlan OffFace = Right;
	OffFace.Portals[0].CenterCm.X = 1;
	Plans[1] = FVoxelLandM5EntranceMaskFixtureFactory::Make(OffFace, Settings, 42, Budget.ToSharedRef());
	D = FVoxelLandM5EntranceMask::Build(
		Size, Origin, 100, Start, Count, Protection, Settings, 42, Plans, Budget.ToSharedRef(), Never, Result);
	TestEqual(TEXT("off-face portal rejected"), D.Code, EVoxelLandM5ErrorCode::OutOfDomain);
	TestTrue(TEXT("off-face retains output"), Result == Previous);
	FVoxelLandM5CavePlan Unaligned = Right;
	++Unaligned.Bounds.MinInclusive.X;
	Plans[1] = FVoxelLandM5EntranceMaskFixtureFactory::Make(Unaligned, Settings, 42, Budget.ToSharedRef());
	D = FVoxelLandM5EntranceMask::Build(
		Size, Origin, 100, Start, Count, Protection, Settings, 42, Plans, Budget.ToSharedRef(), Never, Result);
	TestEqual(TEXT("unaligned plan rejected"), D.Code, EVoxelLandM5ErrorCode::OutOfDomain);
	TestTrue(TEXT("unaligned retains output"), Result == Previous);
	Plans[1] = Plans[0];
	D = FVoxelLandM5EntranceMask::Build(
		Size, Origin, 100, Start, Count, Protection, Settings, 42, Plans, Budget.ToSharedRef(), Never, Result);
	TestEqual(TEXT("duplicate region rejected"), D.Code, EVoxelLandM5ErrorCode::InvalidInput);
	TestTrue(TEXT("duplicate retains output"), Result == Previous);
	return true;
}
#endif
