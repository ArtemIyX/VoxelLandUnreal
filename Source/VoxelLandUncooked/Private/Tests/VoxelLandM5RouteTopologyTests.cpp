#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5RouteTopology.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Misc/AutomationTest.h"

class FVoxelLandM5RouteTopologyFixtureFactory
{
public:
	static TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Make(const FVoxelLandM5CavePlan& Plan,
		const FVoxelLandM5CaveSettings& Settings, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget)
	{
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
		if (!Budget->TryReserveBytes(65536, Lease))
		{
			return nullptr;
		}
		FVoxelLandM5CavePlan Copy = Plan;
		return MakeShareable(
			new FVoxelLandM5CandidateResult(Budget, Settings, 42, Plan.Region, MoveTemp(Copy), MoveTemp(Lease)));
	}
};

namespace
{
constexpr EAutomationTestFlags M5RouteTopologyFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
FVoxelLandM5Endpoint RouteNode(uint64 Id, EVoxelLandM5AnchorRole Role)
{
	FVoxelLandM5Endpoint E;
	E.bValid = true;
	E.Kind = EVoxelLandM5EndpointKind::Chamber;
	E.Id = Id;
	E.Region.X = -1;
	E.AnchorRole = Role;
	return E;
}
FVoxelLandM5RouteFeatureRef RouteFeature(EVoxelLandM5RouteFeatureKind Kind, uint64 Id)
{
	FVoxelLandM5RouteFeatureRef F;
	F.bValid = true;
	F.Kind = Kind;
	F.Id = Id;
	F.Region.X = -1;
	return F;
}
FVoxelLandM5CavePlan RouteFixture()
{
	FVoxelLandM5CavePlan P;
	P.Region.X = -1;
	P.Bounds.MinInclusive.X = -1870;
	P.Bounds.MaxExclusive.X = 130;
	P.Bounds.MinInclusive.Y = -1000;
	P.Bounds.MaxExclusive.Y = 1000;
	P.Bounds.MinInclusive.Z = -1000;
	P.Bounds.MaxExclusive.Z = 1000;
	FVoxelLandM5Chamber A;
	A.bHasId = true;
	A.Id = 0;
	A.bHasFeetAnchor = true;
	P.Chambers.Add(A);
	FVoxelLandM5Chamber B;
	B.bHasId = true;
	B.Id = 7;
	B.CenterCm.X = 40;
	B.bHasFeetAnchor = true;
	B.FeetAnchorCm.X = 40;
	P.Chambers.Add(B);
	FVoxelLandM5Connection C;
	C.bHasId = true;
	C.Id = 5;
	C.Start = RouteNode(0, EVoxelLandM5AnchorRole::None);
	C.End = RouteNode(7, EVoxelLandM5AnchorRole::None);
	P.Connections.Add(C);
	FVoxelLandM5Segment S;
	S.bHasId = true;
	S.Id = 9;
	S.bHasConnectionId = true;
	S.ConnectionId = 5;
	S.RadiusCm = 100;
	S.EndCm.X = 40;
	P.Segments.Add(S);
	FVoxelLandM5Route R;
	R.bHasId = true;
	R.Id = 0;
	R.Start = RouteNode(0, EVoxelLandM5AnchorRole::ChamberFeet);
	R.End = RouteNode(7, EVoxelLandM5AnchorRole::ChamberFeet);
	R.CoveredFeatures.Add(RouteFeature(EVoxelLandM5RouteFeatureKind::Chamber, 0));
	R.CoveredFeatures.Add(RouteFeature(EVoxelLandM5RouteFeatureKind::Connection, 5));
	R.CoveredFeatures.Add(RouteFeature(EVoxelLandM5RouteFeatureKind::Segment, 9));
	R.CoveredFeatures.Add(RouteFeature(EVoxelLandM5RouteFeatureKind::Chamber, 7));
	for (int32 I = 0; I < 4; ++I)
	{
		FVoxelLandM5RouteLeg Leg;
		Leg.StartFeetCm.X = I * 10;
		Leg.EndFeetCm.X = (I + 1) * 10;
		Leg.Feature = R.CoveredFeatures[2];
		R.Legs.Add(Leg);
	}
	P.Routes.Add(MoveTemp(R));
	return P;
}
FVoxelLandM5Endpoint TypedNode(EVoxelLandM5EndpointKind Kind, uint64 Id, EVoxelLandM5AnchorRole Role)
{
	auto E = RouteNode(Id, Role);
	E.Kind = Kind;
	return E;
}
void AddCoveredRoute(FVoxelLandM5CavePlan& Plan, uint64 RouteId, uint64 ConnectionId, uint64 SegmentId,
	const FVoxelLandM5Endpoint& Start, const FVoxelLandM5Endpoint& End, int64 StartX, int64 EndX,
	EVoxelLandM5RouteFeatureKind TerminalKind, uint64 TerminalId)
{
	FVoxelLandM5Connection C;
	C.bHasId = true;
	C.Id = ConnectionId;
	C.Start = Start;
	C.End = End;
	C.Start.AnchorRole = EVoxelLandM5AnchorRole::None;
	C.End.AnchorRole = EVoxelLandM5AnchorRole::None;
	Plan.Connections.Add(C);
	FVoxelLandM5Segment S;
	S.bHasId = true;
	S.Id = SegmentId;
	S.bHasConnectionId = true;
	S.ConnectionId = ConnectionId;
	S.RadiusCm = 100;
	S.StartCm.X = StartX;
	S.EndCm.X = EndX;
	Plan.Segments.Add(S);
	FVoxelLandM5Route R;
	R.bHasId = true;
	R.Id = RouteId;
	R.Start = Start;
	R.End = End;
	R.CoveredFeatures.Add(RouteFeature(EVoxelLandM5RouteFeatureKind::Connection, ConnectionId));
	R.CoveredFeatures.Add(RouteFeature(EVoxelLandM5RouteFeatureKind::Segment, SegmentId));
	R.CoveredFeatures.Add(RouteFeature(TerminalKind, TerminalId));
	for (int32 I = 0; I < 3; ++I)
	{
		FVoxelLandM5RouteLeg Leg;
		Leg.StartFeetCm.X = StartX + I * 10;
		Leg.EndFeetCm.X = StartX + (I + 1) * 10;
		Leg.Feature = R.CoveredFeatures[1];
		R.Legs.Add(Leg);
	}
	Plan.Routes.Add(MoveTemp(R));
}
FVoxelLandM5CavePlan FullRouteFixture()
{
	auto P = RouteFixture();
	FVoxelLandM5Entrance E;
	E.bHasId = true;
	E.Id = 3;
	E.bHasConnectionId = true;
	E.ConnectionId = 6;
	E.bHasInsideFeetAnchor = true;
	E.InsideFeetAnchorCm.X = 60;
	E.bHasOutsideFeetAnchor = true;
	E.OutsideFeetAnchorCm.X = 70;
	E.OpeningBoundsCm.MinInclusive.X = 60;
	E.OpeningBoundsCm.MinInclusive.Y = -1;
	E.OpeningBoundsCm.MinInclusive.Z = -1;
	E.OpeningBoundsCm.MaxExclusive.X = 71;
	E.OpeningBoundsCm.MaxExclusive.Y = 1;
	E.OpeningBoundsCm.MaxExclusive.Z = 1;
	P.Entrances.Add(E);
	FVoxelLandM5Junction J;
	J.bHasId = true;
	J.Id = 4;
	J.bHasCenter = true;
	J.CenterCm.X = 100;
	J.CenterCm.Z = 100;
	J.bHasFeetAnchor = true;
	J.FeetAnchorCm.X = 100;
	P.Junctions.Add(J);
	FVoxelLandM5Portal Portal;
	Portal.bHasId = true;
	Portal.Id = 0xcd359f8c604b6336ULL;
	Portal.CenterCm.X = 130;
	Portal.RadiusCm = 400;
	Portal.Key.WorldSeed = 42;
	Portal.Key.StageId = FGuid(1, 2, 3, 4);
	Portal.Key.RegionA.X = -1;
	Portal.Key.RegionB.X = 0;
	Portal.Key.Axis = EVoxelLandM5PortalAxis::X;
	Portal.bHasRegionAFeetAnchor = true;
	Portal.RegionAFeetAnchorCm.X = 130;
	P.Portals.Add(Portal);
	AddCoveredRoute(P, 1, 6, 10, TypedNode(EVoxelLandM5EndpointKind::Chamber, 7, EVoxelLandM5AnchorRole::ChamberFeet),
		TypedNode(EVoxelLandM5EndpointKind::Entrance, 3, EVoxelLandM5AnchorRole::EntranceOutsideFeet), 40, 70,
		EVoxelLandM5RouteFeatureKind::Entrance, 3);
	AddCoveredRoute(P, 2, 7, 11,
		TypedNode(EVoxelLandM5EndpointKind::Entrance, 3, EVoxelLandM5AnchorRole::EntranceOutsideFeet),
		TypedNode(EVoxelLandM5EndpointKind::Junction, 4, EVoxelLandM5AnchorRole::JunctionFeet), 70, 100,
		EVoxelLandM5RouteFeatureKind::Junction, 4);
	AddCoveredRoute(P, 3, 8, 12, TypedNode(EVoxelLandM5EndpointKind::Junction, 4, EVoxelLandM5AnchorRole::JunctionFeet),
		TypedNode(EVoxelLandM5EndpointKind::Portal, Portal.Id, EVoxelLandM5AnchorRole::PortalRegionAFeet), 100, 130,
		EVoxelLandM5RouteFeatureKind::Portal, Portal.Id);
	// The cave centerline is vertically distinct from the player's feet anchor.
	P.Segments[2].EndCm.Z = 100;
	P.Segments[3].StartCm.Z = 100;
	return P;
}
FVoxelLandM5Diagnostic ValidateFixture(const FVoxelLandM5CavePlan& Plan, TFunctionRef<bool()> Cancel,
	int64 Work = 10000000, bool bStructureOnly = false, int64 OriginX = 130)
{
	FVoxelLandM5CaveSettings S;
	S.StageId = FGuid(1, 2, 3, 4);
	S.PlanningMode = EVoxelLandM5PlanningMode::Regional;
	S.RegionSizeCm = 2000;
	S.RegionOriginXCm = OriginX;
	S.RegionOriginYCm = -1000;
	S.Bounds.MinInclusive.X = OriginX - 2000;
	S.Bounds.MaxExclusive.X = OriginX + 2000;
	S.Bounds.MinInclusive.Y = -1000;
	S.Bounds.MaxExclusive.Y = 3000;
	S.Bounds.MinInclusive.Z = -1000;
	S.Bounds.MaxExclusive.Z = 1000;
	S.Limits.MaxValidationWork = Work;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!FVoxelLandM5Budget::TryCreate(S.Limits.MaxBytes, Work, Budget))
	{
		FVoxelLandM5Diagnostic D;
		D.Code = EVoxelLandM5ErrorCode::InvalidInput;
		return D;
	}
	auto Candidate = FVoxelLandM5RouteTopologyFixtureFactory::Make(Plan, S, Budget.ToSharedRef());
	if (!Candidate.IsValid())
	{
		FVoxelLandM5Diagnostic D;
		D.Code = EVoxelLandM5ErrorCode::BudgetExceeded;
		return D;
	}
	if (bStructureOnly)
	{
		return VoxelLand::M5::RouteTopology::ValidateStructure(
			S, 42, Plan.Region, Candidate.ToSharedRef(), Budget.ToSharedRef(), Cancel);
	}
	return VoxelLand::M5::RouteTopology::Validate(
		S, 42, Plan.Region, Candidate.ToSharedRef(), Budget.ToSharedRef(), Cancel);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5RouteTopologyTest, "VoxelLand.M5.RouteTopology.Structure", M5RouteTopologyFlags)
bool FVoxelLandM5RouteTopologyTest::RunTest(const FString& Parameters)
{
	const auto Never = [] {
		return false;
	};
	const auto Expected = [this, &Never](
							  const TCHAR* Label, const FVoxelLandM5CavePlan& P, EVoxelLandM5ErrorCode Code) {
		TestTrue(Label, ValidateFixture(P, Never).Code == Code);
	};
	const auto Good = RouteFixture();
	Expected(TEXT("structurally valid route remains unproven"), Good, EVoxelLandM5ErrorCode::Unproven);
	auto Single = Good;
	Single.Routes[0].Legs.SetNum(1);
	Single.Routes[0].Legs[0].EndFeetCm.X = 40;
	Expected(TEXT("one segment and one leg can cover both endpoint chambers and the connection"), Single,
		EVoxelLandM5ErrorCode::Unproven);
	const auto Full = FullRouteFixture();
	Expected(TEXT("all structural features covered; geometry unproven"), Full, EVoxelLandM5ErrorCode::Unproven);
	TestTrue(TEXT("structural-only API accepts the complete fixture"),
		ValidateFixture(Full, Never, 10000000, true).Code == EVoxelLandM5ErrorCode::None);
	auto Missing = Full;
	Missing.Junctions[0].bHasCenter = false;
	Expected(TEXT("junction needs a center for segment chains"), Missing, EVoxelLandM5ErrorCode::InconsistentTopology);
	TestTrue(TEXT("structural-only API rejects the missing center"),
		ValidateFixture(Missing, Never, 10000000, true).Code == EVoxelLandM5ErrorCode::InconsistentTopology);
	Missing = Full;
	Missing.Junctions[0].CenterCm.Z = 0;
	Expected(
		TEXT("junction feet cannot replace centerline geometry"), Missing, EVoxelLandM5ErrorCode::InconsistentTopology);
	Missing = Full;
	Missing.Entrances[0].bHasOutsideFeetAnchor = false;
	Expected(TEXT("entrance requires both feet anchors"), Missing, EVoxelLandM5ErrorCode::InconsistentTopology);
	Missing = Full;
	Missing.Entrances[0].InsideFeetAnchorCm.X = 55;
	Expected(TEXT("entrance route must reach inside anchor"), Missing, EVoxelLandM5ErrorCode::InconsistentTopology);
	Missing = Full;
	Missing.Segments[1].StartCm.X = 41;
	Expected(TEXT("segment chain must meet chamber geometry"), Missing, EVoxelLandM5ErrorCode::InconsistentTopology);
	Missing = Full;
	Missing.Segments.RemoveAt(1);
	Expected(TEXT("every connection needs a segment chain"), Missing, EVoxelLandM5ErrorCode::InconsistentTopology);
	Missing = Full;
	Missing.Routes[1].CoveredFeatures[2].Id = 99;
	Expected(TEXT("missing entrance feature rejected"), Missing, EVoxelLandM5ErrorCode::InconsistentTopology);
	Missing = Full;
	Missing.Routes[2].CoveredFeatures[2].Id = 99;
	Expected(TEXT("missing junction feature rejected"), Missing, EVoxelLandM5ErrorCode::InconsistentTopology);
	Missing = Full;
	Missing.Routes[3].CoveredFeatures[2].Id = 99;
	Expected(TEXT("missing portal feature rejected"), Missing, EVoxelLandM5ErrorCode::InconsistentTopology);
	Missing = Full;
	Missing.Routes[3].End.AnchorRole = EVoxelLandM5AnchorRole::PortalRegionBFeet;
	Expected(TEXT("wrong portal region anchor rejected"), Missing, EVoxelLandM5ErrorCode::InconsistentTopology);
	Missing = Full;
	Missing.Portals[0].Key.Salt = 1;
	Expected(TEXT("foreign portal key rejected"), Missing, EVoxelLandM5ErrorCode::InvalidInput);
	Missing = Full;
	Missing.Portals[0].CenterCm.X = 129;
	Expected(TEXT("off-face portal rejected on negative side"), Missing, EVoxelLandM5ErrorCode::OutOfDomain);
	Missing = Full;
	Missing.Portals[0].CenterCm.X = 131;
	Expected(TEXT("off-face portal rejected on positive side"), Missing, EVoxelLandM5ErrorCode::OutOfDomain);
	Missing = Full;
	Missing.Portals[0].CenterCm.Y = 700;
	Expected(TEXT("portal radius must fit the tangential region"), Missing, EVoxelLandM5ErrorCode::OutOfDomain);
	Missing = Full;
	Missing.Portals[0].RadiusCm = 0;
	Expected(TEXT("zero portal radius rejected"), Missing, EVoxelLandM5ErrorCode::InvalidInput);
	Missing = Full;
	Missing.Portals[0].CenterCm.X = MAX_int64;
	Expected(TEXT("portal extent arithmetic overflow rejected"), Missing, EVoxelLandM5ErrorCode::InvalidInput);
	Missing = Full;
	Missing.Bounds.MinInclusive.X = -1871;
	Expected(TEXT("candidate bounds must match its grid region"), Missing, EVoxelLandM5ErrorCode::InconsistentTopology);
	Missing = Full;
	Missing.Portals[0].CenterCm.X = -130;
	Missing.Segments[3].EndCm.X = -130;
	Missing.Bounds.MinInclusive.X = -2130;
	Missing.Bounds.MaxExclusive.X = -130;
	TestTrue(TEXT("correct negative shared face is accepted structurally"),
		ValidateFixture(Missing, Never, 10000000, true, -130).Code == EVoxelLandM5ErrorCode::None);
	Missing = Full;
	Missing.Region.X = MAX_int64 - 1;
	Missing.Portals[0].Key.RegionA.X = MAX_int64 - 1;
	Missing.Portals[0].Key.RegionB.X = MAX_int64;
	Missing.Portals[0].Id = 0xd2e7d91b751ada66ULL; // Independent SHA-256 packet vector.
	TestTrue(TEXT("region multiplication overflow is rejected"),
		ValidateFixture(Missing, Never, 10000000, true).Code == EVoxelLandM5ErrorCode::OutOfDomain);
	Missing = Full;
	Missing.Entrances[0].OutsideFeetAnchorCm.X = 71;
	Missing.Routes[2].Legs[0].StartFeetCm.X = 71;
	Expected(TEXT("different entrance anchors need a connector"), Missing, EVoxelLandM5ErrorCode::InconsistentTopology);
	auto P = Good;
	P.Chambers[0].bHasFeetAnchor = false;
	Expected(TEXT("missing anchor"), P, EVoxelLandM5ErrorCode::InconsistentTopology);
	P = Good;
	P.Routes[0].Start.AnchorRole = EVoxelLandM5AnchorRole::EntranceInsideFeet;
	Expected(TEXT("wrong anchor role"), P, EVoxelLandM5ErrorCode::InconsistentTopology);
	P = Good;
	P.Routes[0].Start.Region.X = 0;
	Expected(TEXT("wrong region"), P, EVoxelLandM5ErrorCode::InconsistentTopology);
	P = Good;
	P.Routes[0].Legs[1].StartFeetCm.X = 11;
	Expected(TEXT("gap between legs"), P, EVoxelLandM5ErrorCode::InconsistentTopology);
	P = Good;
	P.Routes[0].CoveredFeatures.RemoveAt(2);
	Expected(TEXT("missing segment coverage"), P, EVoxelLandM5ErrorCode::InconsistentTopology);
	P = Good;
	P.Routes[0].CoveredFeatures.Add(P.Routes[0].CoveredFeatures[0]);
	Expected(TEXT("duplicate coverage owner"), P, EVoxelLandM5ErrorCode::InconsistentTopology);
	P = Good;
	P.Segments[0].ConnectionId = 99;
	Expected(TEXT("dangling segment connection"), P, EVoxelLandM5ErrorCode::InconsistentTopology);
	P = Good;
	P.Routes[0].Legs[0].Transition = static_cast<EVoxelLandM5RouteTransition>(255);
	Expected(TEXT("unknown transition"), P, EVoxelLandM5ErrorCode::InconsistentTopology);
	P = Good;
	P.Routes[0].Legs[0].EndFeetCm = P.Routes[0].Legs[0].StartFeetCm;
	Expected(TEXT("zero length leg"), P, EVoxelLandM5ErrorCode::InconsistentTopology);
	TestTrue(TEXT("insufficient work fails before validation"),
		ValidateFixture(Good, Never, 1).Code == EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("structural validation shares the work limit"),
		ValidateFixture(Good, Never, 1, true).Code == EVoxelLandM5ErrorCode::BudgetExceeded);
	const auto Cancel = [] {
		return true;
	};
	TestTrue(TEXT("cancellation is explicit"), ValidateFixture(Good, Cancel).Code == EVoxelLandM5ErrorCode::Cancelled);
	TestTrue(TEXT("structural cancellation is explicit"),
		ValidateFixture(Good, Cancel, 10000000, true).Code == EVoxelLandM5ErrorCode::Cancelled);
	return true;
}
#endif
