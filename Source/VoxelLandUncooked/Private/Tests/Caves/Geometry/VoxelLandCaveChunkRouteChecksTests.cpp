#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Data/Meshing/VoxelLandMeshData.h"
#include "Data/Meshing/VoxelLandMeshingScratch.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Planning/VoxelLandCaveCandidateResult.h"
#include "Generation/Caves/Geometry/VoxelLandCaveChunkRouteChecks.h"
#include "Generation/Caves/Geometry/VoxelLandCaveIdealRouteValidation.h"
#include "Generation/Caves/Field/VoxelLandCaveMeshAdapter.h"
#include "Generation/Caves/Geometry/VoxelLandCaveRouteBuilder.h"
#include "Generation/Caves/Geometry/VoxelLandCaveSingleChunkRoute.h"
#include "Generation/Meshing/VoxelLandMeshing.h"
#include "Misc/AutomationTest.h"

class FVoxelLandM5ChunkRoutesFixtureFactory
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

namespace
{
FVoxelLandChunkData M5ChunkRoutesPlane(bool bObstacle)
{
	FVoxelLandChunkData Chunk;
	Chunk.Density.SetNumUninitialized(FVoxelLandChunkData::SampleCount);
	for (int32 Z = -1; Z <= 33; ++Z)
	{
		for (int32 Y = -1; Y <= 33; ++Y)
		{
			for (int32 X = -1; X <= 33; ++X)
			{
				const bool bSolid = Z <= 8 || (bObstacle && X == 16 && Z >= 10 && Z <= 11);
				Chunk.Density[(X + 1) + 35 * ((Y + 1) + 35 * (Z + 1))] = bSolid ? -100 : 100;
			}
		}
	}
	return Chunk;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5ChunkRoutesTest, "VoxelLand.M5.Routes.AllChunkIntervals",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM5ChunkRoutesTest::RunTest(const FString& Parameters)
{
	const auto Never = [] {
		return false;
	};
	FVoxelLandM5CaveSettings Settings;
	// Positive support-face clearance remains unproven; this fixture exercises zero-margin routes.
	Settings.Capsule.ClearanceMarginCm = 0;
	Settings.StageId = FGuid(1, 2, 3, 4);
	Settings.Topology.MinChambers = 2;
	Settings.Topology.MaxChambers = 3;
	Settings.Topology.EntranceCount = 0;
	Settings.Topology.MinLoops = 0;
	Settings.Topology.MaxLoops = 0;
	Settings.Topology.MinChamberRadiusCm = 150;
	Settings.Topology.MaxChamberRadiusCm = 150;
	Settings.Topology.MinTunnelRadiusCm = 103;
	Settings.Topology.MaxTunnelRadiusCm = 103;
	Settings.Limits.MaxBytes = 1024ll * 1024 * 1024;
	Settings.Limits.MaxValidationWork = 1000000000;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"),
			FVoxelLandM5Budget::TryCreate(Settings.Limits.MaxBytes, Settings.Limits.MaxValidationWork, Budget)))
	{
		return false;
	}
	FVoxelLandM5CavePlan Plan;
	FVoxelLandM5Chamber A;
	A.bHasId = true;
	A.Id = 1;
	A.CenterCm.X = 1300;
	A.CenterCm.Y = 1450;
	A.CenterCm.Z = 942;
	A.RadiusCm = 150;
	FVoxelLandM5Chamber B = A;
	B.Id = 2;
	B.CenterCm.X = 1700;
	Plan.Chambers = {A, B};
	FVoxelLandM5Connection Connection;
	Connection.bHasId = true;
	Connection.Id = 7;
	Connection.Start.bValid = true;
	Connection.Start.Id = A.Id;
	Connection.End.bValid = true;
	Connection.End.Id = B.Id;
	Plan.Connections.Add(Connection);
	FVoxelLandM5Segment First;
	First.bHasId = true;
	First.Id = 9;
	First.bHasConnectionId = true;
	First.ConnectionId = Connection.Id;
	First.StartCm = A.CenterCm;
	First.EndCm = A.CenterCm;
	First.EndCm.X = 1500;
	First.RadiusCm = 103;
	FVoxelLandM5Segment Second = First;
	Second.Id = 10;
	Second.StartCm = First.EndCm;
	Second.EndCm = B.CenterCm;
	Plan.Segments = {First, Second};
	auto Candidate = FVoxelLandM5ChunkRoutesFixtureFactory::Make(Settings, Budget.ToSharedRef(), Plan);
	if (!TestTrue(TEXT("candidate admitted"), Candidate.IsValid()))
	{
		return false;
	}
	FVoxelLandM5RouteAnchorChoice Start;
	Start.Endpoint = Connection.Start;
	Start.Endpoint.AnchorRole = EVoxelLandM5AnchorRole::ChamberFeet;
	Start.FeetCm = A.CenterCm;
	Start.FeetCm.Z = 850;
	FVoxelLandM5RouteAnchorChoice End = Start;
	End.Endpoint = Connection.End;
	End.Endpoint.AnchorRole = EVoxelLandM5AnchorRole::ChamberFeet;
	End.FeetCm.X = 1700;
	const TArray<FVoxelLandM5RouteAnchorChoice> Anchors{Start, End};
	FVoxelLandM5RouteConnectionChoice Choice;
	Choice.bHasConnectionId = true;
	Choice.ConnectionId = Connection.Id;
	Choice.Start = Start.Endpoint;
	Choice.End = End.Endpoint;
	Choice.SegmentIds = {9, 10};
	const TArray<FVoxelLandM5RouteConnectionChoice> Choices{Choice};
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Built;
	const auto BuiltError = VoxelLand::M5::RouteBuilder::Build(
		Settings, 11, Plan.Region, Candidate.ToSharedRef(), Anchors, Choices, Budget.ToSharedRef(), Never, Built);
	if (!TestTrue(TEXT("route builder succeeds"), BuiltError.Code == EVoxelLandM5ErrorCode::None)
		|| !TestTrue(TEXT("route exists"),
			Built.IsValid() && Built->GetPlan().Routes.Num() == 1 && Built->GetPlan().Routes[0].Legs.Num() == 2))
	{
		return false;
	}
	const auto MakeSelections = [&](const FVoxelLandChunkData& Chunk, TArray<FVoxelLandM5RouteSupportSelection>& Out) {
		FVoxelLandMeshingScratch Scratch;
		FVoxelLandMeshData Mesh;
		if (VoxelLandMeshing::BuildSurfaceNets(Chunk, 100.f, Scratch, Mesh, Never) != EVoxelLandMeshResult::Success)
		{
			AddError(TEXT("fixture meshing failed"));
			return false;
		}
		TArray<FVoxelLandM5ValidationTriangle> Adapted;
		if (VoxelLand::M5::MeshAdapter::Adapt(Chunk, 100.f, false, Mesh, 196608, 294912, Never, Adapted).Code
			!= EVoxelLandM5ErrorCode::None)
		{
			AddError(TEXT("fixture adaptation failed"));
			return false;
		}
		TArray<int32> Faces;
		for (int32 Index = 0; Index < Adapted.Num(); ++Index)
		{
			const auto& T = Adapted[Index];
			if (T.A.Z == 850 && T.B.Z == 850 && T.C.Z == 850 && T.A.X >= 1050 && T.A.X <= 1950 && T.B.X >= 1050
				&& T.B.X <= 1950 && T.C.X >= 1050 && T.C.X <= 1950 && T.A.Y >= 1150 && T.A.Y <= 1750 && T.B.Y >= 1150
				&& T.B.Y <= 1750 && T.C.Y >= 1150 && T.C.Y <= 1750)
			{
				Faces.Add(Index);
			}
		}
		if (!TestTrue(TEXT("bounded support faces"), !Faces.IsEmpty() && Faces.Num() <= 256))
		{
			return false;
		}
		for (const FVoxelLandM5Route& Route : Built->GetPlan().Routes)
		{
			for (int32 Leg = 0; Leg < Route.Legs.Num(); ++Leg)
			{
				FVoxelLandM5RouteSupportSelection Selection;
				Selection.RouteId = Route.Id;
				Selection.LegIndex = Leg;
				Selection.SupportIndices = Faces;
				Out.Add(MoveTemp(Selection));
			}
		}
		return true;
	};
	const FVoxelLandChunkData Flat = M5ChunkRoutesPlane(false);
	TArray<FVoxelLandM5RouteSupportSelection> Selections;
	if (!MakeSelections(Flat, Selections))
	{
		return false;
	}
	const auto Check = [&](const FVoxelLandChunkData& Chunk, TConstArrayView<FVoxelLandM5RouteSupportSelection> Inputs,
						   TFunctionRef<bool()> Cancel) {
		return VoxelLand::M5::ChunkRouteChecks::CheckAll(
			Settings, 11, Plan.Region, Built.ToSharedRef(), Chunk, 100, false, 0, Inputs, Budget.ToSharedRef(), Cancel)
			.Code;
	};
	TestTrue(TEXT("all route legs are checked against actual flat mesh"),
		Check(Flat, Selections, Never) == EVoxelLandM5ErrorCode::None);
	TArray<FVoxelLandM5RouteSupportSelection> Missing = Selections;
	Missing.RemoveAt(1);
	TestTrue(TEXT("missing leg cannot pass"), Check(Flat, Missing, Never) == EVoxelLandM5ErrorCode::InvalidInput);
	TArray<FVoxelLandM5RouteSupportSelection> Duplicate = Selections;
	Duplicate[1].LegIndex = 0;
	TestTrue(TEXT("duplicate leg cannot pass"), Check(Flat, Duplicate, Never) == EVoxelLandM5ErrorCode::InvalidInput);
	TArray<FVoxelLandM5RouteSupportSelection> Wrong = Selections;
	++Wrong[1].RouteId;
	TestTrue(TEXT("foreign route cannot pass"), Check(Flat, Wrong, Never) == EVoxelLandM5ErrorCode::InvalidInput);
	const FVoxelLandChunkData Obstructed = M5ChunkRoutesPlane(true);
	TArray<FVoxelLandM5RouteSupportSelection> ObstructedSelections;
	if (!MakeSelections(Obstructed, ObstructedSelections))
	{
		return false;
	}
	TestTrue(TEXT("obstacle on second leg rejects all-route check"),
		Check(Obstructed, ObstructedSelections, Never) == EVoxelLandM5ErrorCode::Unproven);
	const auto Cancel = [] {
		return true;
	};
	TestTrue(TEXT("cancellation propagates"), Check(Flat, Selections, Cancel) == EVoxelLandM5ErrorCode::Cancelled);

	// Two separate routes both have a leg numbered zero. Distinct route IDs
	// are essential: a lookup based on leg index alone would duplicate one
	// selection and miss the other route.
	FVoxelLandM5CavePlan TwoPlan;
	FVoxelLandM5Chamber Middle = A;
	Middle.Id = 3;
	Middle.CenterCm.X = 1500;
	TwoPlan.Chambers = {A, Middle, B};
	FVoxelLandM5Connection LeftConnection = Connection;
	LeftConnection.End.Id = Middle.Id;
	FVoxelLandM5Connection RightConnection = Connection;
	RightConnection.Id = 8;
	RightConnection.Start.Id = Middle.Id;
	TwoPlan.Connections = {LeftConnection, RightConnection};
	FVoxelLandM5Segment LeftSegment = First;
	FVoxelLandM5Segment RightSegment = Second;
	RightSegment.ConnectionId = RightConnection.Id;
	TwoPlan.Segments = {LeftSegment, RightSegment};
	auto TwoCandidate = FVoxelLandM5ChunkRoutesFixtureFactory::Make(Settings, Budget.ToSharedRef(), TwoPlan);
	if (!TestTrue(TEXT("two-route candidate admitted"), TwoCandidate.IsValid()))
	{
		return false;
	}
	FVoxelLandM5RouteAnchorChoice Mid = Start;
	Mid.Endpoint.Id = Middle.Id;
	Mid.FeetCm.X = 1500;
	const TArray<FVoxelLandM5RouteAnchorChoice> TwoAnchors{Start, Mid, End};
	FVoxelLandM5RouteConnectionChoice LeftChoice = Choice;
	LeftChoice.End = Mid.Endpoint;
	LeftChoice.SegmentIds = {LeftSegment.Id};
	FVoxelLandM5RouteConnectionChoice RightChoice = Choice;
	RightChoice.ConnectionId = RightConnection.Id;
	RightChoice.Start = Mid.Endpoint;
	RightChoice.SegmentIds = {RightSegment.Id};
	const TArray<FVoxelLandM5RouteConnectionChoice> TwoChoices{LeftChoice, RightChoice};
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> TwoBuilt;
	const auto TwoError = VoxelLand::M5::RouteBuilder::Build(Settings, 11, TwoPlan.Region, TwoCandidate.ToSharedRef(),
		TwoAnchors, TwoChoices, Budget.ToSharedRef(), Never, TwoBuilt);
	if (!TestTrue(TEXT("two-route builder succeeds"), TwoError.Code == EVoxelLandM5ErrorCode::None)
		|| !TestTrue(TEXT("two routes each have one leg"),
			TwoBuilt.IsValid() && TwoBuilt->GetPlan().Routes.Num() == 2 && TwoBuilt->GetPlan().Routes[0].Legs.Num() == 1
				&& TwoBuilt->GetPlan().Routes[1].Legs.Num() == 1
				&& TwoBuilt->GetPlan().Routes[0].Id != TwoBuilt->GetPlan().Routes[1].Id))
	{
		return false;
	}
	Built = TwoBuilt;
	TArray<FVoxelLandM5RouteSupportSelection> TwoSelections;
	if (!MakeSelections(Flat, TwoSelections))
	{
		return false;
	}
	Swap(TwoSelections[0], TwoSelections[1]);
	TestTrue(TEXT("route ID disambiguates equal leg indices independent of selection order"),
		Check(Flat, TwoSelections, Never) == EVoxelLandM5ErrorCode::None);
	TArray<FVoxelLandM5RouteSupportSelection> RepeatedRoute = TwoSelections;
	RepeatedRoute[1].RouteId = RepeatedRoute[0].RouteId;
	TestTrue(TEXT("duplicate route and leg cannot hide the other route"),
		Check(Flat, RepeatedRoute, Never) == EVoxelLandM5ErrorCode::InvalidInput);

	// Count polls separately for the deterministic ideal proof and a successful
	// first mesh interval. CheckAll performs one initial poll, one per route in
	// its count pass and one per selection during each lookup. Cancellation at
	// the next poll occurs in the second lookup after the first interval returns.
	int32 IdealPolls = 0;
	const auto CountIdeal = [&IdealPolls] {
		++IdealPolls;
		return false;
	};
	const auto Ideal = VoxelLand::M5::IdealRouteValidation::Validate(
		Settings, 11, TwoPlan.Region, Built.ToSharedRef(), Budget.ToSharedRef(), CountIdeal);
	if (!TestTrue(TEXT("two-route ideal validation succeeds"), Ideal.Code == EVoxelLandM5ErrorCode::None))
	{
		return false;
	}
	const FVoxelLandM5Route& FirstRoute = Built->GetPlan().Routes[0];
	const FVoxelLandM5RouteSupportSelection* FirstSupport = nullptr;
	for (const auto& Selection : TwoSelections)
	{
		if (Selection.RouteId == FirstRoute.Id && Selection.LegIndex == 0)
		{
			FirstSupport = &Selection;
		}
	}
	if (!TestNotNull(TEXT("first route support found"), FirstSupport))
	{
		return false;
	}
	int32 SinglePolls = 0;
	const auto CountSingle = [&SinglePolls] {
		++SinglePolls;
		return false;
	};
	const auto Single = VoxelLand::M5::SingleChunkRoute::Certify(Flat, 100, false, FirstRoute.Legs[0].StartFeetCm,
		FirstRoute.Legs[0].EndFeetCm, Settings.Capsule.RadiusCm, Settings.Capsule.StandingHeightCm,
		Settings.Capsule.ClearanceMarginCm, 0, Settings.Capsule.MaxSlopeDegrees, 90, FirstSupport->SupportIndices,
		Budget.ToSharedRef(), CountSingle);
	if (!TestTrue(TEXT("first interval independently succeeds"), Single.Code == EVoxelLandM5ErrorCode::None))
	{
		return false;
	}
	const int32 CancelAt = 1 + IdealPolls + TwoBuilt->GetPlan().Routes.Num() + TwoSelections.Num() + SinglePolls + 1;
	int32 CheckPolls = 0;
	const auto CancelAfterFirst = [&CheckPolls, CancelAt] {
		return ++CheckPolls == CancelAt;
	};
	TestTrue(TEXT("cancellation after first successful interval propagates"),
		Check(Flat, TwoSelections, CancelAfterFirst) == EVoxelLandM5ErrorCode::Cancelled);
	TestEqual(TEXT("cancellation reached second lookup"), CheckPolls, CancelAt);
	return true;
}
#endif
