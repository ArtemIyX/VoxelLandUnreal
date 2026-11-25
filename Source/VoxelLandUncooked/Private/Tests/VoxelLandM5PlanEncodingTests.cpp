#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Structs/VoxelLandM5CavePlan.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5PlanEncoding.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5PlanEncodingTest, "VoxelLand.M5.PlanEncoding.Identity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5PlanEncodingTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::PlanEncoding;
	auto EncodePlan = [this](const FVoxelLandM5CavePlan& Plan, TArray<uint8>& Bytes) {
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
		if (!FVoxelLandM5Budget::TryCreate(4 * 1024 * 1024, 4000000, Budget)) return false;
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
		const auto D = Encode(Plan, Budget.ToSharedRef(), [] { return false; }, Bytes, Lease);
		return TestTrue(TEXT("plan encoding succeeds"), D.Code == EVoxelLandM5ErrorCode::None && Lease.IsValid());
	};
	FVoxelLandM5CavePlan Empty;
	TArray<uint8> EmptyBytes;
	if (!EncodePlan(Empty, EmptyBytes)) return false;
	TestEqual(TEXT("empty packet fixed size"), EmptyBytes.Num(), 104);
	if (EmptyBytes.Num() != 104) return false;
	TestTrue(TEXT("versioned tag and geometry are little endian"), EmptyBytes[0] == 'V' && EmptyBytes[1] == 'L'
		&& EmptyBytes[2] == 'P' && EmptyBytes[3] == '5' && EmptyBytes[4] == 1 && EmptyBytes[8] == 1);
	for (int32 I = 9; I < EmptyBytes.Num(); ++I)
		TestEqual(TEXT("empty remainder is zero"), EmptyBytes[I], uint8(0));

	FVoxelLandM5CavePlan One;
	One.Chambers.AddDefaulted();
	One.Chambers[0].bHasId = true;
	One.Chambers[0].Id = 0x0102030405060708ULL;
	One.Chambers[0].CenterCm.X = -1;
	TArray<uint8> OneBytes;
	if (!EncodePlan(One, OneBytes)) return false;
	TestTrue(TEXT("one chamber LE count and flag"), OneBytes[76] == 1 && OneBytes[77] == 0
		&& OneBytes[78] == 0 && OneBytes[79] == 0 && OneBytes[80] == 1);
	for (int32 I = 0; I < 8; ++I)
	{
		TestEqual(TEXT("ID little endian"), OneBytes[81 + I], uint8(8 - I));
		TestEqual(TEXT("negative coordinate two's complement"), OneBytes[89 + I], uint8(255));
	}

	FVoxelLandM5CavePlan Plan;
	Plan.GeometryVersion = 2;
	Plan.Region.X = -2;
	Plan.Chambers.AddDefaulted();
	Plan.Segments.AddDefaulted();
	Plan.Connections.AddDefaulted();
	Plan.Entrances.AddDefaulted();
	Plan.Junctions.AddDefaulted();
	Plan.Portals.AddDefaulted();
	Plan.Routes.AddDefaulted();
	Plan.Routes[0].Legs.AddDefaulted();
	Plan.Routes[0].CoveredFeatures.AddDefaulted();
	TArray<uint8> Canonical, Repeat;
	if (!EncodePlan(Plan, Canonical) || !EncodePlan(Plan, Repeat)) return false;
	TestTrue(TEXT("repeat encoding identical"), Canonical == Repeat);
	auto Changed = [&](const TCHAR* Label, const FVoxelLandM5CavePlan& Other) {
		TArray<uint8> Bytes;
		if (EncodePlan(Other, Bytes)) TestTrue(Label, Bytes != Canonical);
	};
	FVoxelLandM5CavePlan Other = Plan;
	Other.Chambers[0].Profile.FloorCm = -1; Changed(TEXT("chamber profile bound"), Other);
	Other = Plan; Other.Segments[0].Profile.EndCeilingCm = 1; Changed(TEXT("segment profile bound"), Other);
	Other = Plan; Other.Connections[0].End.AnchorRole = EVoxelLandM5AnchorRole::EntranceInsideFeet;
	Changed(TEXT("connection endpoint bound"), Other);
	Other = Plan; Other.Entrances[0].InsideFeetAnchorCm.X = 1; Changed(TEXT("inactive entrance payload bound"), Other);
	Other = Plan; Other.Junctions[0].CenterCm.Z = 1; Changed(TEXT("inactive junction payload bound"), Other);
	Other = Plan; Other.Portals[0].Key.StageId.A = 1; Changed(TEXT("full portal key bound"), Other);
	Other = Plan; Other.Routes[0].Legs[0].SupportPlane.C.Z = 1; Changed(TEXT("inactive support plane bound"), Other);
	Other = Plan; Other.Routes[0].CoveredFeatures[0].Id = 1; Changed(TEXT("covered feature bound"), Other);
	Other = Plan; Other.Routes[0].Legs.AddDefaulted(); Changed(TEXT("nested lengths bound"), Other);

	// Full packet expected bytes independently assembled from the documented LE layout.
	FVoxelLandM5CavePlan Full;
	Full.GeometryVersion = 2;
	auto P1 = [](FVoxelLandM5PointCm& P) { P.X = P.Y = P.Z = 1; };
	auto R1 = [](FVoxelLandM5RegionCoord& R) { R.X = R.Y = 1; };
	auto B1 = [&P1](FVoxelLandM5Bounds& B) { P1(B.MinInclusive); P1(B.MaxExclusive); };
	auto E1 = [&R1](FVoxelLandM5Endpoint& E) { E.bValid = true; E.Kind = static_cast<EVoxelLandM5EndpointKind>(1); E.AnchorRole = static_cast<EVoxelLandM5AnchorRole>(1); R1(E.Region); E.Id = 1; };
	auto F1 = [&R1](FVoxelLandM5RouteFeatureRef& F) { F.bValid = true; F.Kind = static_cast<EVoxelLandM5RouteFeatureKind>(1); R1(F.Region); F.Id = 1; };
	B1(Full.Bounds); R1(Full.Region);
	auto& C = Full.Chambers.AddDefaulted_GetRef(); C.bHasId = true; C.Id = 1; P1(C.CenterCm); C.RadiusCm = 1;
	C.Profile.Version = 2; C.Profile.FloorCm = C.Profile.CeilingCm = 1; C.bHasFeetAnchor = true; P1(C.FeetAnchorCm);
	auto& Link = Full.Connections.AddDefaulted_GetRef(); Link.bHasId = Link.bLoop = true; Link.Id = 1; E1(Link.Start); E1(Link.End);
	auto& S = Full.Segments.AddDefaulted_GetRef(); S.bHasId = S.bHasConnectionId = true; S.Id = S.ConnectionId = 1;
	P1(S.StartCm); P1(S.EndCm); S.RadiusCm = 1; S.Profile.Version = 2;
	S.Profile.StartFloorCm = S.Profile.EndFloorCm = S.Profile.StartCeilingCm = S.Profile.EndCeilingCm = 1;
	auto& Entry = Full.Entrances.AddDefaulted_GetRef(); Entry.bHasId = Entry.bHasConnectionId = Entry.bHasInsideFeetAnchor = Entry.bHasOutsideFeetAnchor = Entry.bFlooded = true;
	Entry.Id = Entry.ConnectionId = 1; B1(Entry.OpeningBoundsCm); P1(Entry.InsideFeetAnchorCm); P1(Entry.OutsideFeetAnchorCm);
	auto& J = Full.Junctions.AddDefaulted_GetRef(); J.bHasId = J.bHasCenter = J.bHasFeetAnchor = true; J.Id = 1; P1(J.CenterCm); P1(J.FeetAnchorCm);
	auto& Portal = Full.Portals.AddDefaulted_GetRef(); Portal.bHasId = Portal.bHasRegionAFeetAnchor = Portal.bHasRegionBFeetAnchor = true;
	Portal.Id = 1; Portal.Key.WorldSeed = Portal.Key.Salt = 1; Portal.Key.StageId = FGuid(1, 1, 1, 1);
	R1(Portal.Key.RegionA); R1(Portal.Key.RegionB); Portal.Key.Axis = static_cast<EVoxelLandM5PortalAxis>(1);
	P1(Portal.CenterCm); Portal.RadiusCm = 1; P1(Portal.RegionAFeetAnchorCm); P1(Portal.RegionBFeetAnchorCm);
	auto& Route = Full.Routes.AddDefaulted_GetRef(); Route.bHasId = true; Route.Id = 1; E1(Route.Start); E1(Route.End);
	auto& Leg = Route.Legs.AddDefaulted_GetRef(); P1(Leg.StartFeetCm); P1(Leg.EndFeetCm);
	Leg.Transition = static_cast<EVoxelLandM5RouteTransition>(1); F1(Leg.Feature); Leg.bHasSupportPlane = true;
	P1(Leg.SupportPlane.A); P1(Leg.SupportPlane.B); P1(Leg.SupportPlane.C); F1(Route.CoveredFeatures.AddDefaulted_GetRef());
	const char* Hex =
		"564c503501000000020000000100000000000000010000000000000001000000000000000100000000000000010000000000"
		"0000010000000000000001000000000000000100000000000000010000000101000000000000000100000000000000010000"
		"0000000000010000000000000001000000020000000100000000000000010000000000000001010000000000000001000000"
		"0000000001000000000000000100000001010000000000000001010101000000000000000100000000000000010000000000"
		"0000010101010000000000000001000000000000000100000000000000010100000001010000000000000001010000000000"
		"0000010000000000000001000000000000000100000000000000010000000000000001000000000000000100000000000000"
		"0100000002000000010000000000000001000000000000000100000000000000010000000000000001000000010100000000"
		"0000000101000000000000000100000000000000010000000000000001000000000000000100000000000000010000000000"
		"0000010000000000000001010000000000000001000000000000000100000000000000010100000000000000010000000000"
		"0000010000000000000001010000000101000000000000000101000000000000000100000000000000010000000000000001"
		"0100000000000000010000000000000001000000000000000100000001010000000000000001000000000000000100000001"
		"0000000100000001000000010000000000000001000000000000000100000000000000010000000000000001000000000000"
		"0001010000000000000001000000000000000100000000000000010000000101000000000000000100000000000000010000"
		"0000000000010100000000000000010000000000000001000000000000000100000001010000000000000001010101000000"
		"0000000001000000000000000100000000000000010101010000000000000001000000000000000100000000000000010000"
		"0001000000000000000100000000000000010000000000000001000000000000000100000000000000010000000000000001"
		"0101010000000000000001000000000000000100000000000000010100000000000000010000000000000001000000000000"
		"0001000000000000000100000000000000010000000000000001000000000000000100000000000000010000000000000001"
		"0000000101010000000000000001000000000000000100000000000000";
	TArray<uint8> FullExpected;
	auto HexValue = [](char V) { return uint8(V <= '9' ? V - '0' : V - 'a' + 10); };
	for (int32 I = 0; Hex[I] != 0; I += 2) FullExpected.Add(uint8((HexValue(Hex[I]) << 4) | HexValue(Hex[I + 1])));
	TArray<uint8> FullActual;
	if (!EncodePlan(Full, FullActual)) return false;
	TestEqual(TEXT("independent complete packet length"), FullActual.Num(), 929);
	TestTrue(TEXT("independent complete nested packet"), FullActual == FullExpected);

	// Each field is independently changed against a one-byte offset oracle.
#define M5_CHECK_VALUE(Field, Offset) do { auto M = Full; M.Field = static_cast<decltype(M.Field)>(3); \
	TArray<uint8> Expect = FullExpected, Actual; Expect[Offset] = 3; \
	if (EncodePlan(M, Actual)) TestTrue(TEXT("field offset"), Actual == Expect); } while (false)
#define M5_CHECK_BOOL(Field, Offset) do { auto M = Full; M.Field = false; \
	TArray<uint8> Expect = FullExpected, Actual; Expect[Offset] = 0; \
	if (EncodePlan(M, Actual)) TestTrue(TEXT("flag offset"), Actual == Expect); } while (false)
	M5_CHECK_VALUE(GeometryVersion, 8);
	M5_CHECK_VALUE(Bounds.MinInclusive.X, 12);
	M5_CHECK_VALUE(Bounds.MinInclusive.Y, 20);
	M5_CHECK_VALUE(Bounds.MinInclusive.Z, 28);
	M5_CHECK_VALUE(Bounds.MaxExclusive.X, 36);
	M5_CHECK_VALUE(Bounds.MaxExclusive.Y, 44);
	M5_CHECK_VALUE(Bounds.MaxExclusive.Z, 52);
	M5_CHECK_VALUE(Region.X, 60);
	M5_CHECK_VALUE(Region.Y, 68);
	M5_CHECK_BOOL(Chambers[0].bHasId, 80);
	M5_CHECK_VALUE(Chambers[0].Id, 81);
	M5_CHECK_VALUE(Chambers[0].CenterCm.X, 89);
	M5_CHECK_VALUE(Chambers[0].CenterCm.Y, 97);
	M5_CHECK_VALUE(Chambers[0].CenterCm.Z, 105);
	M5_CHECK_VALUE(Chambers[0].RadiusCm, 113);
	M5_CHECK_VALUE(Chambers[0].Profile.Version, 117);
	M5_CHECK_VALUE(Chambers[0].Profile.FloorCm, 121);
	M5_CHECK_VALUE(Chambers[0].Profile.CeilingCm, 129);
	M5_CHECK_BOOL(Chambers[0].bHasFeetAnchor, 137);
	M5_CHECK_VALUE(Chambers[0].FeetAnchorCm.X, 138);
	M5_CHECK_VALUE(Chambers[0].FeetAnchorCm.Y, 146);
	M5_CHECK_VALUE(Chambers[0].FeetAnchorCm.Z, 154);
	M5_CHECK_BOOL(Connections[0].bHasId, 166);
	M5_CHECK_VALUE(Connections[0].Id, 167);
	M5_CHECK_BOOL(Connections[0].Start.bValid, 175);
	M5_CHECK_VALUE(Connections[0].Start.Kind, 176);
	M5_CHECK_VALUE(Connections[0].Start.AnchorRole, 177);
	M5_CHECK_VALUE(Connections[0].Start.Region.X, 178);
	M5_CHECK_VALUE(Connections[0].Start.Region.Y, 186);
	M5_CHECK_VALUE(Connections[0].Start.Id, 194);
	M5_CHECK_BOOL(Connections[0].End.bValid, 202);
	M5_CHECK_VALUE(Connections[0].End.Kind, 203);
	M5_CHECK_VALUE(Connections[0].End.AnchorRole, 204);
	M5_CHECK_VALUE(Connections[0].End.Region.X, 205);
	M5_CHECK_VALUE(Connections[0].End.Region.Y, 213);
	M5_CHECK_VALUE(Connections[0].End.Id, 221);
	M5_CHECK_BOOL(Connections[0].bLoop, 229);
	M5_CHECK_BOOL(Segments[0].bHasId, 234);
	M5_CHECK_VALUE(Segments[0].Id, 235);
	M5_CHECK_BOOL(Segments[0].bHasConnectionId, 243);
	M5_CHECK_VALUE(Segments[0].ConnectionId, 244);
	M5_CHECK_VALUE(Segments[0].StartCm.X, 252);
	M5_CHECK_VALUE(Segments[0].StartCm.Y, 260);
	M5_CHECK_VALUE(Segments[0].StartCm.Z, 268);
	M5_CHECK_VALUE(Segments[0].EndCm.X, 276);
	M5_CHECK_VALUE(Segments[0].EndCm.Y, 284);
	M5_CHECK_VALUE(Segments[0].EndCm.Z, 292);
	M5_CHECK_VALUE(Segments[0].RadiusCm, 300);
	M5_CHECK_VALUE(Segments[0].Profile.Version, 304);
	M5_CHECK_VALUE(Segments[0].Profile.StartFloorCm, 308);
	M5_CHECK_VALUE(Segments[0].Profile.EndFloorCm, 316);
	M5_CHECK_VALUE(Segments[0].Profile.StartCeilingCm, 324);
	M5_CHECK_VALUE(Segments[0].Profile.EndCeilingCm, 332);
	M5_CHECK_BOOL(Entrances[0].bHasId, 344);
	M5_CHECK_VALUE(Entrances[0].Id, 345);
	M5_CHECK_BOOL(Entrances[0].bHasConnectionId, 353);
	M5_CHECK_VALUE(Entrances[0].ConnectionId, 354);
	M5_CHECK_VALUE(Entrances[0].OpeningBoundsCm.MinInclusive.X, 362);
	M5_CHECK_VALUE(Entrances[0].OpeningBoundsCm.MinInclusive.Y, 370);
	M5_CHECK_VALUE(Entrances[0].OpeningBoundsCm.MinInclusive.Z, 378);
	M5_CHECK_VALUE(Entrances[0].OpeningBoundsCm.MaxExclusive.X, 386);
	M5_CHECK_VALUE(Entrances[0].OpeningBoundsCm.MaxExclusive.Y, 394);
	M5_CHECK_VALUE(Entrances[0].OpeningBoundsCm.MaxExclusive.Z, 402);
	M5_CHECK_BOOL(Entrances[0].bHasInsideFeetAnchor, 410);
	M5_CHECK_VALUE(Entrances[0].InsideFeetAnchorCm.X, 411);
	M5_CHECK_VALUE(Entrances[0].InsideFeetAnchorCm.Y, 419);
	M5_CHECK_VALUE(Entrances[0].InsideFeetAnchorCm.Z, 427);
	M5_CHECK_BOOL(Entrances[0].bHasOutsideFeetAnchor, 435);
	M5_CHECK_VALUE(Entrances[0].OutsideFeetAnchorCm.X, 436);
	M5_CHECK_VALUE(Entrances[0].OutsideFeetAnchorCm.Y, 444);
	M5_CHECK_VALUE(Entrances[0].OutsideFeetAnchorCm.Z, 452);
	M5_CHECK_BOOL(Entrances[0].bFlooded, 460);
	M5_CHECK_BOOL(Junctions[0].bHasId, 465);
	M5_CHECK_VALUE(Junctions[0].Id, 466);
	M5_CHECK_BOOL(Junctions[0].bHasCenter, 474);
	M5_CHECK_VALUE(Junctions[0].CenterCm.X, 475);
	M5_CHECK_VALUE(Junctions[0].CenterCm.Y, 483);
	M5_CHECK_VALUE(Junctions[0].CenterCm.Z, 491);
	M5_CHECK_BOOL(Junctions[0].bHasFeetAnchor, 499);
	M5_CHECK_VALUE(Junctions[0].FeetAnchorCm.X, 500);
	M5_CHECK_VALUE(Junctions[0].FeetAnchorCm.Y, 508);
	M5_CHECK_VALUE(Junctions[0].FeetAnchorCm.Z, 516);
	M5_CHECK_BOOL(Portals[0].bHasId, 528);
	M5_CHECK_VALUE(Portals[0].Id, 529);
	M5_CHECK_VALUE(Portals[0].Key.WorldSeed, 537);
	M5_CHECK_VALUE(Portals[0].Key.StageId.A, 545);
	M5_CHECK_VALUE(Portals[0].Key.StageId.B, 549);
	M5_CHECK_VALUE(Portals[0].Key.StageId.C, 553);
	M5_CHECK_VALUE(Portals[0].Key.StageId.D, 557);
	M5_CHECK_VALUE(Portals[0].Key.Salt, 561);
	M5_CHECK_VALUE(Portals[0].Key.RegionA.X, 569);
	M5_CHECK_VALUE(Portals[0].Key.RegionA.Y, 577);
	M5_CHECK_VALUE(Portals[0].Key.RegionB.X, 585);
	M5_CHECK_VALUE(Portals[0].Key.RegionB.Y, 593);
	M5_CHECK_VALUE(Portals[0].Key.Axis, 601);
	M5_CHECK_VALUE(Portals[0].CenterCm.X, 602);
	M5_CHECK_VALUE(Portals[0].CenterCm.Y, 610);
	M5_CHECK_VALUE(Portals[0].CenterCm.Z, 618);
	M5_CHECK_VALUE(Portals[0].RadiusCm, 626);
	M5_CHECK_BOOL(Portals[0].bHasRegionAFeetAnchor, 630);
	M5_CHECK_VALUE(Portals[0].RegionAFeetAnchorCm.X, 631);
	M5_CHECK_VALUE(Portals[0].RegionAFeetAnchorCm.Y, 639);
	M5_CHECK_VALUE(Portals[0].RegionAFeetAnchorCm.Z, 647);
	M5_CHECK_BOOL(Portals[0].bHasRegionBFeetAnchor, 655);
	M5_CHECK_VALUE(Portals[0].RegionBFeetAnchorCm.X, 656);
	M5_CHECK_VALUE(Portals[0].RegionBFeetAnchorCm.Y, 664);
	M5_CHECK_VALUE(Portals[0].RegionBFeetAnchorCm.Z, 672);
	M5_CHECK_BOOL(Routes[0].bHasId, 684);
	M5_CHECK_VALUE(Routes[0].Id, 685);
	M5_CHECK_BOOL(Routes[0].Start.bValid, 693);
	M5_CHECK_VALUE(Routes[0].Start.Kind, 694);
	M5_CHECK_VALUE(Routes[0].Start.AnchorRole, 695);
	M5_CHECK_VALUE(Routes[0].Start.Region.X, 696);
	M5_CHECK_VALUE(Routes[0].Start.Region.Y, 704);
	M5_CHECK_VALUE(Routes[0].Start.Id, 712);
	M5_CHECK_BOOL(Routes[0].End.bValid, 720);
	M5_CHECK_VALUE(Routes[0].End.Kind, 721);
	M5_CHECK_VALUE(Routes[0].End.AnchorRole, 722);
	M5_CHECK_VALUE(Routes[0].End.Region.X, 723);
	M5_CHECK_VALUE(Routes[0].End.Region.Y, 731);
	M5_CHECK_VALUE(Routes[0].End.Id, 739);
	M5_CHECK_VALUE(Routes[0].Legs[0].StartFeetCm.X, 751);
	M5_CHECK_VALUE(Routes[0].Legs[0].StartFeetCm.Y, 759);
	M5_CHECK_VALUE(Routes[0].Legs[0].StartFeetCm.Z, 767);
	M5_CHECK_VALUE(Routes[0].Legs[0].EndFeetCm.X, 775);
	M5_CHECK_VALUE(Routes[0].Legs[0].EndFeetCm.Y, 783);
	M5_CHECK_VALUE(Routes[0].Legs[0].EndFeetCm.Z, 791);
	M5_CHECK_VALUE(Routes[0].Legs[0].Transition, 799);
	M5_CHECK_BOOL(Routes[0].Legs[0].Feature.bValid, 800);
	M5_CHECK_VALUE(Routes[0].Legs[0].Feature.Kind, 801);
	M5_CHECK_VALUE(Routes[0].Legs[0].Feature.Region.X, 802);
	M5_CHECK_VALUE(Routes[0].Legs[0].Feature.Region.Y, 810);
	M5_CHECK_VALUE(Routes[0].Legs[0].Feature.Id, 818);
	M5_CHECK_BOOL(Routes[0].Legs[0].bHasSupportPlane, 826);
	M5_CHECK_VALUE(Routes[0].Legs[0].SupportPlane.A.X, 827);
	M5_CHECK_VALUE(Routes[0].Legs[0].SupportPlane.A.Y, 835);
	M5_CHECK_VALUE(Routes[0].Legs[0].SupportPlane.A.Z, 843);
	M5_CHECK_VALUE(Routes[0].Legs[0].SupportPlane.B.X, 851);
	M5_CHECK_VALUE(Routes[0].Legs[0].SupportPlane.B.Y, 859);
	M5_CHECK_VALUE(Routes[0].Legs[0].SupportPlane.B.Z, 867);
	M5_CHECK_VALUE(Routes[0].Legs[0].SupportPlane.C.X, 875);
	M5_CHECK_VALUE(Routes[0].Legs[0].SupportPlane.C.Y, 883);
	M5_CHECK_VALUE(Routes[0].Legs[0].SupportPlane.C.Z, 891);
	M5_CHECK_BOOL(Routes[0].CoveredFeatures[0].bValid, 903);
	M5_CHECK_VALUE(Routes[0].CoveredFeatures[0].Kind, 904);
	M5_CHECK_VALUE(Routes[0].CoveredFeatures[0].Region.X, 905);
	M5_CHECK_VALUE(Routes[0].CoveredFeatures[0].Region.Y, 913);
	M5_CHECK_VALUE(Routes[0].CoveredFeatures[0].Id, 921);
#undef M5_CHECK_VALUE
#undef M5_CHECK_BOOL

	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> EmptyBudget;
	if (!FVoxelLandM5Budget::TryCreate(0, 0, EmptyBudget)) return false;
	TArray<uint8> Preserved = {71};
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> NoLease;
	const auto NoWork = Encode(Plan, EmptyBudget.ToSharedRef(), [] { return false; }, Preserved, NoLease);
	TestTrue(TEXT("budget rejected"), NoWork.Code == EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("failed encoding preserves outputs"), Preserved == TArray<uint8>({71}) && !NoLease);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> CancelBudget;
	if (!FVoxelLandM5Budget::TryCreate(100000, 100000, CancelBudget)) return false;
	int32 Polls = 0;
	const auto MidCancel = Encode(Plan, CancelBudget.ToSharedRef(), [&Polls] { return ++Polls == 4; }, Preserved, NoLease);
	TestTrue(TEXT("mid-encoding cancellation reported"), MidCancel.Code == EVoxelLandM5ErrorCode::Cancelled);
	TestTrue(TEXT("mid-encoding preserves outputs"), Preserved == TArray<uint8>({71}) && !NoLease);
	const auto Cancel = Encode(Plan, EmptyBudget.ToSharedRef(), [] { return true; }, Preserved, NoLease);
	TestTrue(TEXT("cancellation reported"), Cancel.Code == EVoxelLandM5ErrorCode::Cancelled);
	TestTrue(TEXT("cancellation preserves outputs"), Preserved == TArray<uint8>({71}) && !NoLease);
	return true;
}
#endif
