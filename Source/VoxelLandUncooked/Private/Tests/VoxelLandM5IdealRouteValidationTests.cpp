#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5IdealRouteValidation.h"
#include "Generation/VoxelLandM5RouteBuilder.h"
#include "Generation/VoxelLandM5Seed.h"
#include "Misc/AutomationTest.h"

class FVoxelLandM5IdealRouteFixtureFactory
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
		return MakeShareable(
			new FVoxelLandM5CandidateResult(Budget, Settings, 11, Region, MoveTemp(Plan), MoveTemp(Lease)));
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5IdealRouteValidationTest, "VoxelLand.M5.Routes.IdealValidation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM5IdealRouteValidationTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings Settings;
	Settings.StageId = FGuid(1, 2, 3, 4);
	Settings.Topology.MinChambers = 2;
	Settings.Topology.MaxChambers = 2;
	Settings.Topology.EntranceCount = 0;
	Settings.Topology.MinLoops = 0;
	Settings.Topology.MaxLoops = 0;
	Settings.Topology.MinChamberRadiusCm = 150;
	Settings.Topology.MaxChamberRadiusCm = 150;
	Settings.Topology.MinTunnelRadiusCm = 103;
	Settings.Topology.MaxTunnelRadiusCm = 103;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!FVoxelLandM5Budget::TryCreate(Settings.Limits.MaxBytes, Settings.Limits.MaxValidationWork, Budget))
	{
		AddError(TEXT("Unable to create budget"));
		return false;
	}
	FVoxelLandM5CavePlan Plan;
	Plan.Region.X = -1;
	FVoxelLandM5Chamber A;
	A.bHasId = true;
	A.Id = 1;
	A.CenterCm.X = -200;
	A.CenterCm.Z = 200;
	A.RadiusCm = 150;
	FVoxelLandM5Chamber B = A;
	B.Id = 2;
	B.CenterCm.X = 200;
	Plan.Chambers = {A, B};
	FVoxelLandM5Connection Connection;
	Connection.bHasId = true;
	Connection.Id = 7;
	Connection.Start.bValid = true;
	Connection.Start.Id = 1;
	Connection.Start.Region = Plan.Region;
	Connection.End.bValid = true;
	Connection.End.Id = 2;
	Connection.End.Region = Plan.Region;
	Plan.Connections.Add(Connection);
	FVoxelLandM5Segment First;
	First.bHasId = true;
	First.Id = 9;
	First.bHasConnectionId = true;
	First.ConnectionId = 7;
	First.StartCm = A.CenterCm;
	First.EndCm.Z = 200;
	First.RadiusCm = 103;
	FVoxelLandM5Segment Second = First;
	Second.Id = 10;
	Second.StartCm = First.EndCm;
	Second.EndCm = B.CenterCm;
	Plan.Segments = {First, Second};
	auto Candidate = FVoxelLandM5IdealRouteFixtureFactory::Make(Settings, Budget.ToSharedRef(), Plan);
	if (!TestTrue(TEXT("source candidate admitted"), Candidate.IsValid()))
	{
		return false;
	}
	FVoxelLandM5RouteAnchorChoice Start;
	Start.Endpoint = Connection.Start;
	Start.Endpoint.AnchorRole = EVoxelLandM5AnchorRole::ChamberFeet;
	Start.FeetCm = A.CenterCm;
	Start.FeetCm.Z = 108; // 200 - floor(185 / 2).
	FVoxelLandM5RouteAnchorChoice End = Start;
	End.Endpoint = Connection.End;
	End.Endpoint.AnchorRole = EVoxelLandM5AnchorRole::ChamberFeet;
	End.FeetCm.X = 200;
	TArray<FVoxelLandM5RouteAnchorChoice> Anchors{Start, End};
	FVoxelLandM5RouteConnectionChoice Choice;
	Choice.bHasConnectionId = true;
	Choice.ConnectionId = 7;
	Choice.Start = Start.Endpoint;
	Choice.End = End.Endpoint;
	Choice.SegmentIds = {9, 10};
	TArray<FVoxelLandM5RouteConnectionChoice> Choices{Choice};
	const auto Never = [] {
		return false;
	};
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Built;
	const auto BuildError = VoxelLand::M5::RouteBuilder::Build(
		Settings, 11, Plan.Region, Candidate.ToSharedRef(), Anchors, Choices, Budget.ToSharedRef(), Never, Built);
	if (!TestTrue(TEXT("real route builder succeeds"), BuildError.Code == EVoxelLandM5ErrorCode::None)
		|| !TestTrue(TEXT("route result exists"), Built.IsValid()))
	{
		return false;
	}
	const auto Validate = [&](const FVoxelLandM5CaveSettings& FixtureSettings,
							  TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Value) {
		return VoxelLand::M5::IdealRouteValidation::Validate(
			FixtureSettings, 11, Plan.Region, Value, Budget.ToSharedRef(), Never)
			.Code;
	};
	TestTrue(
		TEXT("built segment routes have ideal coverage"), Validate(Settings, Built.ToSharedRef()) == EVoxelLandM5ErrorCode::None);
	FVoxelLandM5CavePlan Modified = Built->GetPlan();
	if (!TestTrue(TEXT("two route legs"), Modified.Routes.Num() == 1 && Modified.Routes[0].Legs.Num() == 2))
	{
		return false;
	}
	// Preserve endpoint anchors and leg continuity but move the common point
	// off both segment proposals. A structural label alone must not certify it.
	Modified.Routes[0].Legs[0].EndFeetCm.Y = 1;
	Modified.Routes[0].Legs[1].StartFeetCm.Y = 1;
	auto Moved = FVoxelLandM5IdealRouteFixtureFactory::Make(Settings, Budget.ToSharedRef(), Modified);
	if (!TestTrue(TEXT("moved fixture admitted"), Moved.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("moved but connected legs fail ideal proof"),
		Validate(Settings, Moved.ToSharedRef()) == EVoxelLandM5ErrorCode::Unproven);
	Modified = Built->GetPlan();
	Modified.Routes[0].Legs[0].Feature.Id = 10;
	auto Mislabeled = FVoxelLandM5IdealRouteFixtureFactory::Make(Settings, Budget.ToSharedRef(), Modified);
	if (!TestTrue(TEXT("mislabeled fixture admitted"), Mislabeled.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("mislabeled segment fails"), Validate(Settings, Mislabeled.ToSharedRef()) != EVoxelLandM5ErrorCode::None);
	Modified = Built->GetPlan();
	Modified.Routes[0].Legs.RemoveAt(0);
	auto Missing = FVoxelLandM5IdealRouteFixtureFactory::Make(Settings, Budget.ToSharedRef(), Modified);
	if (!TestTrue(TEXT("partial fixture admitted"), Missing.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("partial route fails"), Validate(Settings, Missing.ToSharedRef()) != EVoxelLandM5ErrorCode::None);
	Modified = Built->GetPlan();
	const FVoxelLandM5RouteLeg Forward = Modified.Routes[0].Legs[0];
	FVoxelLandM5RouteLeg Reverse = Forward;
	Swap(Reverse.StartFeetCm, Reverse.EndFeetCm);
	Modified.Routes[0].Legs.Insert(Reverse, 1);
	Modified.Routes[0].Legs.Insert(Forward, 2);
	auto Duplicate = FVoxelLandM5IdealRouteFixtureFactory::Make(Settings, Budget.ToSharedRef(), Modified);
	if (!TestTrue(TEXT("duplicate traversal fixture admitted"), Duplicate.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("duplicate segment traversal fails"),
		Validate(Settings, Duplicate.ToSharedRef()) == EVoxelLandM5ErrorCode::InconsistentTopology);

	// Independent cumulative admission for the real two-segment route:
	// structure = (1+1) + 64*(2+1+2+1+2+5+1)^2 + 4096 = 16642;
	// ideal count preflight 2, scans 46, two segment checks 2*(8+32) = 80.
	// The complete validation therefore consumes exactly 16770 work units.
	const auto CheckWork = [&](int64 Limit, EVoxelLandM5ErrorCode Expected, int64 ExpectedUsed) {
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Bounded;
		if (!FVoxelLandM5Budget::TryCreate(Settings.Limits.MaxBytes, Limit, Bounded))
		{
			AddError(TEXT("bounded budget allocation failed"));
			return;
		}
		auto Fixture = FVoxelLandM5IdealRouteFixtureFactory::Make(Settings, Bounded.ToSharedRef(), Built->GetPlan());
		if (!TestTrue(TEXT("bounded route fixture admitted"), Fixture.IsValid()))
		{
			return;
		}
		const auto Result = VoxelLand::M5::IdealRouteValidation::Validate(
			Settings, 11, Plan.Region, Fixture.ToSharedRef(), Bounded.ToSharedRef(), Never);
		TestTrue(TEXT("cumulative boundary status"), Result.Code == Expected);
		int64 Live = 0, Peak = 0, Work = 0;
		Bounded->GetUsage(Live, Peak, Work);
		TestEqual(TEXT("cumulative boundary charged work"), Work, ExpectedUsed);
	};
	CheckWork(16770, EVoxelLandM5ErrorCode::None, 16770);
	CheckWork(16769, EVoxelLandM5ErrorCode::BudgetExceeded, 16738);

	// Translate the real route onto an aligned regional domain: the portal is
	// on X=0 between regions (-1,0)/(0,0), 1000 cm inside the Y face span.
	FVoxelLandM5CaveSettings RegionalSettings = Settings;
	RegionalSettings.PlanningMode = EVoxelLandM5PlanningMode::Regional;
	RegionalSettings.Topology.MinChambers = 1;
	RegionalSettings.Topology.MaxChambers = 1;
	RegionalSettings.Topology.MinTunnelRadiusCm = 250;
	RegionalSettings.Topology.MaxTunnelRadiusCm = 250;
	RegionalSettings.Bounds.MinInclusive.X = -204800;
	RegionalSettings.Bounds.MinInclusive.Y = 0;
	RegionalSettings.Bounds.MinInclusive.Z = -1000;
	RegionalSettings.Bounds.MaxExclusive.X = 204800;
	RegionalSettings.Bounds.MaxExclusive.Y = 204800;
	RegionalSettings.Bounds.MaxExclusive.Z = 1000;
	Modified = Built->GetPlan();
	Modified.Bounds = RegionalSettings.Bounds;
	Modified.Bounds.MaxExclusive.X = 0;
	for (auto& Chamber : Modified.Chambers)
	{
		Chamber.CenterCm.X -= 200;
		Chamber.FeetAnchorCm.X -= 200;
		Chamber.CenterCm.Y += 1000;
		Chamber.FeetAnchorCm.Y += 1000;
	}
	for (auto& Segment : Modified.Segments)
	{
		Segment.StartCm.X -= 200;
		Segment.EndCm.X -= 200;
		Segment.StartCm.Y += 1000;
		Segment.EndCm.Y += 1000;
		Segment.RadiusCm = 250;
	}
	for (auto& Route : Modified.Routes)
	{
		for (auto& Leg : Route.Legs)
		{
			Leg.StartFeetCm.X -= 200;
			Leg.EndFeetCm.X -= 200;
			Leg.StartFeetCm.Y += 1000;
			Leg.EndFeetCm.Y += 1000;
		}
	}
	Modified.Chambers.RemoveAt(1);
	FVoxelLandM5Portal Portal;
	Portal.bHasId = true;
	Portal.Key.WorldSeed = 11;
	Portal.Key.StageId = Settings.StageId;
	Portal.Key.Salt = Settings.Salt;
	Portal.Key.RegionA = Plan.Region;
	Portal.Key.RegionB.X = 0;
	Portal.Key.Axis = EVoxelLandM5PortalAxis::X;
	Portal.CenterCm = B.CenterCm;
	Portal.CenterCm.X -= 200;
	Portal.CenterCm.Y += 1000;
	Portal.RadiusCm = 250;
	Portal.bHasRegionAFeetAnchor = true;
	Portal.RegionAFeetAnchorCm = End.FeetCm;
	Portal.RegionAFeetAnchorCm.X -= 200;
	Portal.RegionAFeetAnchorCm.Y += 1000;
	TArray<uint8> PortalDigest;
	if (!TestTrue(TEXT("portal digest"), VoxelLand::M5::Seed::TryPortalDigest(11, Settings.StageId,
											 static_cast<uint64>(Settings.Salt), -1, 0, 0, 0, PortalDigest))
		|| !TestTrue(TEXT("portal ID"), VoxelLand::M5::Seed::TryDigestWord(PortalDigest, Portal.Id)))
	{
		return false;
	}
	Modified.Portals.Add(Portal);
	Modified.Connections[0].End.Kind = EVoxelLandM5EndpointKind::Portal;
	Modified.Connections[0].End.Id = Portal.Id;
	Modified.Routes[0].End.Kind = EVoxelLandM5EndpointKind::Portal;
	Modified.Routes[0].End.Id = Portal.Id;
	Modified.Routes[0].End.AnchorRole = EVoxelLandM5AnchorRole::PortalRegionAFeet;
	for (auto& Feature : Modified.Routes[0].CoveredFeatures)
	{
		if (Feature.Kind == EVoxelLandM5RouteFeatureKind::Chamber && Feature.Id == B.Id)
		{
			Feature.Kind = EVoxelLandM5RouteFeatureKind::Portal;
			Feature.Id = Portal.Id;
		}
	}
	auto PortalPlan = FVoxelLandM5IdealRouteFixtureFactory::Make(RegionalSettings, Budget.ToSharedRef(), Modified);
	if (!TestTrue(TEXT("regional portal fixture admitted"), PortalPlan.IsValid()))
	{
		return false;
	}

	TestTrue(TEXT("region-A portal route verifies"),
		Validate(RegionalSettings, PortalPlan.ToSharedRef()) == EVoxelLandM5ErrorCode::None);
	Modified.Portals[0].RegionAFeetAnchorCm.Y += 1;
	auto BadPortal = FVoxelLandM5IdealRouteFixtureFactory::Make(RegionalSettings, Budget.ToSharedRef(), Modified);
	if (!TestTrue(TEXT("bad portal fixture admitted"), BadPortal.IsValid()))
	{
		return false;
	}
	TestTrue(
		TEXT("portal anchor mismatch fails"), Validate(RegionalSettings, BadPortal.ToSharedRef()) != EVoxelLandM5ErrorCode::None);

	// Replace the terminal chamber with an entrance whose inside anchor is at
	// the connected leg junction, and outside anchor is the route endpoint.
	FVoxelLandM5CaveSettings EntranceSettings = Settings;
	EntranceSettings.Topology.MinChambers = 1;
	EntranceSettings.Topology.MaxChambers = 1;
	EntranceSettings.Topology.EntranceCount = 1;
	Modified = Built->GetPlan();
	Modified.Chambers.RemoveAt(1);
	FVoxelLandM5Entrance Entrance;
	Entrance.bHasId = true;
	Entrance.Id = 3;
	Entrance.bHasConnectionId = true;
	Entrance.ConnectionId = 7;
	Entrance.bHasInsideFeetAnchor = true;
	Entrance.InsideFeetAnchorCm = Modified.Routes[0].Legs[0].EndFeetCm;
	Entrance.bHasOutsideFeetAnchor = true;
	Entrance.OutsideFeetAnchorCm = End.FeetCm;
	Entrance.OpeningBoundsCm.MinInclusive.X = 199;
	Entrance.OpeningBoundsCm.MinInclusive.Y = -1;
	Entrance.OpeningBoundsCm.MinInclusive.Z = 199;
	Entrance.OpeningBoundsCm.MaxExclusive.X = 201;
	Entrance.OpeningBoundsCm.MaxExclusive.Y = 1;
	Entrance.OpeningBoundsCm.MaxExclusive.Z = 201;
	Modified.Entrances.Add(Entrance);
	Modified.Connections[0].End.Kind = EVoxelLandM5EndpointKind::Entrance;
	Modified.Connections[0].End.Id = Entrance.Id;
	Modified.Routes[0].End.Kind = EVoxelLandM5EndpointKind::Entrance;
	Modified.Routes[0].End.Id = Entrance.Id;
	Modified.Routes[0].End.AnchorRole = EVoxelLandM5AnchorRole::EntranceOutsideFeet;
	for (auto& Feature : Modified.Routes[0].CoveredFeatures)
	{
		if (Feature.Kind == EVoxelLandM5RouteFeatureKind::Chamber && Feature.Id == B.Id)
		{
			Feature.Kind = EVoxelLandM5RouteFeatureKind::Entrance;
			Feature.Id = Entrance.Id;
		}
	}
	auto EntrancePlan = FVoxelLandM5IdealRouteFixtureFactory::Make(EntranceSettings, Budget.ToSharedRef(), Modified);
	if (!TestTrue(TEXT("routed entrance fixture admitted"), EntrancePlan.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("both entrance anchors lie on connected route"),
		Validate(EntranceSettings, EntrancePlan.ToSharedRef()) == EVoxelLandM5ErrorCode::None);
	Modified.Entrances[0].InsideFeetAnchorCm.Y = 1;
	auto BadEntrance = FVoxelLandM5IdealRouteFixtureFactory::Make(EntranceSettings, Budget.ToSharedRef(), Modified);
	if (!TestTrue(TEXT("bad entrance fixture admitted"), BadEntrance.IsValid()))
	{
		return false;
	}
	TestTrue(
		TEXT("missing entrance traversal fails"), Validate(EntranceSettings, BadEntrance.ToSharedRef()) != EVoxelLandM5ErrorCode::None);
	const auto Cancelled = [] {
		return true;
	};
	TestTrue(TEXT("cancellation propagates"), VoxelLand::M5::IdealRouteValidation::Validate(Settings, 11, Plan.Region,
												  Built.ToSharedRef(), Budget.ToSharedRef(), Cancelled)
													  .Code
												  == EVoxelLandM5ErrorCode::Cancelled);
	TestTrue(TEXT("source mismatch rejected"), VoxelLand::M5::IdealRouteValidation::Validate(Settings, 12, Plan.Region,
												   Built.ToSharedRef(), Budget.ToSharedRef(), Never)
													   .Code
												   == EVoxelLandM5ErrorCode::InvalidInput);
	return true;
}
#endif
