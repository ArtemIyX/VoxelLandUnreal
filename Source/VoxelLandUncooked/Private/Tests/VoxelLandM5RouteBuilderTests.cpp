#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5RouteBuilder.h"
#include "Generation/VoxelLandM5PortalHandshake.h"
#include "Generation/VoxelLandM5Seed.h"
#include "Generation/VoxelLandM5RouteTopology.h"
#include "Misc/AutomationTest.h"

class FVoxelLandM5RouteBuilderFixtureFactory
{
public:
	static TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> MakePlan(
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
	static TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Make(
		const FVoxelLandM5CaveSettings& Settings, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget)
	{
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
		if (!Budget->TryReserveBytes(65536, Lease))
		{
			return nullptr;
		}
		FVoxelLandM5CavePlan Plan;
		FVoxelLandM5Chamber A;
		A.bHasId = true;
		A.Id = 1;
		A.CenterCm.X = -200;
		A.CenterCm.Z = 200;
		A.RadiusCm = 150;
		FVoxelLandM5Chamber B = A;
		B.Id = 2;
		B.CenterCm.X = 200;
		Plan.Chambers.Add(A);
		Plan.Chambers.Add(B);
		FVoxelLandM5Connection C;
		C.bHasId = true;
		C.Id = 7;
		C.Start.bValid = true;
		C.Start.Id = 1;
		C.End.bValid = true;
		C.End.Id = 2;
		Plan.Connections.Add(C);
		FVoxelLandM5Segment S;
		S.bHasId = true;
		S.Id = 9;
		S.bHasConnectionId = true;
		S.ConnectionId = 7;
		S.StartCm = A.CenterCm;
		S.EndCm = B.CenterCm;
		S.RadiusCm = 103;
		Plan.Segments.Add(S);
		return MakeShareable(
			new FVoxelLandM5CandidateResult(Budget, Settings, 11, Plan.Region, MoveTemp(Plan), MoveTemp(Lease)));
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5RouteBuilderTest, "VoxelLand.M5.Routes.ExplicitBuilder",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM5RouteBuilderTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings Settings;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget"),
			FVoxelLandM5Budget::TryCreate(Settings.Limits.MaxBytes, Settings.Limits.MaxValidationWork, Budget))
		|| !Budget.IsValid())
	{
		return false;
	}
	auto Candidate = FVoxelLandM5RouteBuilderFixtureFactory::Make(Settings, Budget.ToSharedRef());
	if (!TestTrue(TEXT("candidate"), Candidate.IsValid()))
	{
		return false;
	}
	FVoxelLandM5RouteAnchorChoice A;
	A.Endpoint.bValid = true;
	A.Endpoint.Id = 1;
	A.Endpoint.AnchorRole = EVoxelLandM5AnchorRole::ChamberFeet;
	A.FeetCm.X = -200;
	A.FeetCm.Z = 108;
	FVoxelLandM5RouteAnchorChoice B = A;
	B.Endpoint.Id = 2;
	B.FeetCm.X = 200;
	TArray<FVoxelLandM5RouteAnchorChoice> Anchors{A, B};
	FVoxelLandM5RouteConnectionChoice C;
	C.bHasConnectionId = true;
	C.ConnectionId = 7;
	C.Start = A.Endpoint;
	C.End = B.Endpoint;
	C.SegmentIds.Add(9);
	TArray<FVoxelLandM5RouteConnectionChoice> Choices{C};
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Result;
	const auto Never = [] {
		return false;
	};
	const auto Build = [&] {
		return VoxelLand::M5::RouteBuilder::Build(Settings, 11, Candidate->GetPlan().Region, Candidate.ToSharedRef(),
			Anchors, Choices, Budget.ToSharedRef(), Never, Result);
	};
	if (!TestTrue(TEXT("explicit route built"), Build().Code == EVoxelLandM5ErrorCode::None)
		|| !TestTrue(TEXT("one route"), Result.IsValid() && Result->GetPlan().Routes.Num() == 1))
	{
		return false;
	}
	const auto& Route = Result->GetPlan().Routes[0];
	TestEqual(TEXT("one leg"), Route.Legs.Num(), 1);
	if (Route.Legs.Num() == 1)
	{
		TestEqual(TEXT("start feet"), Route.Legs[0].StartFeetCm.X, int64(-200));
		TestEqual(TEXT("end feet"), Route.Legs[0].EndFeetCm.X, int64(200));
		TestEqual(TEXT("segment identifier"), Route.Legs[0].Feature.Id, uint64(9));
	}
	auto Retained = Result;
	Anchors[1].FeetCm.Z += 1;
	TestTrue(TEXT("mismatched anchor rejected"), Build().Code == EVoxelLandM5ErrorCode::Unproven);
	TestTrue(TEXT("failure preserves result"), Result == Retained);
	Anchors[1] = B;
	Choices[0].SegmentIds.Add(9);
	TestTrue(TEXT("duplicate segment rejected"), Build().Code != EVoxelLandM5ErrorCode::None);
	TestTrue(TEXT("duplicate preserves result"), Result == Retained);
	Choices[0] = C;
	Anchors.Add(A);
	TestTrue(TEXT("duplicate anchor rejected"), Build().Code == EVoxelLandM5ErrorCode::IdentityCollision);
	TestTrue(TEXT("duplicate anchor preserves result"), Result == Retained);
	Anchors.RemoveAt(2);
	int32 Polls = 0;
	const auto CancelDuringAnchors = [&Polls] {
		return ++Polls == 4;
	};
	TestTrue(TEXT("cancellation in construction is reported"),
		VoxelLand::M5::RouteBuilder::Build(Settings, 11, Candidate->GetPlan().Region, Candidate.ToSharedRef(), Anchors,
			Choices, Budget.ToSharedRef(), CancelDuringAnchors, Result)
				.Code
			== EVoxelLandM5ErrorCode::Cancelled);
	TestTrue(TEXT("cancellation preserves result"), Result == Retained);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> SmallMemory;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> SmallWork;
	if (!FVoxelLandM5Budget::TryCreate(65536, Settings.Limits.MaxValidationWork, SmallMemory)
		|| !FVoxelLandM5Budget::TryCreate(Settings.Limits.MaxBytes, 1, SmallWork))
	{
		return false;
	}
	auto MemoryCandidate = FVoxelLandM5RouteBuilderFixtureFactory::Make(Settings, SmallMemory.ToSharedRef());
	auto WorkCandidate = FVoxelLandM5RouteBuilderFixtureFactory::Make(Settings, SmallWork.ToSharedRef());
	if (!MemoryCandidate.IsValid() || !WorkCandidate.IsValid())
	{
		return false;
	}
	TestTrue(TEXT("memory exhaustion rejected"),
		VoxelLand::M5::RouteBuilder::Build(Settings, 11, MemoryCandidate->GetPlan().Region,
			MemoryCandidate.ToSharedRef(), Anchors, Choices, SmallMemory.ToSharedRef(), Never, Result)
				.Code
			== EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("memory exhaustion preserves result"), Result == Retained);
	TestTrue(TEXT("work exhaustion rejected"),
		VoxelLand::M5::RouteBuilder::Build(Settings, 11, WorkCandidate->GetPlan().Region, WorkCandidate.ToSharedRef(),
			Anchors, Choices, SmallWork.ToSharedRef(), Never, Result)
				.Code
			== EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("work exhaustion preserves result"), Result == Retained);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5RouteBuilderJunctionTest, "VoxelLand.M5.Routes.EntranceAndJunction",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM5RouteBuilderJunctionTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings Settings;
	Settings.StageId = FGuid(1, 2, 3, 4);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!FVoxelLandM5Budget::TryCreate(Settings.Limits.MaxBytes, Settings.Limits.MaxValidationWork, Budget))
	{
		return false;
	}
	FVoxelLandM5CavePlan Plan;
	FVoxelLandM5Chamber Chamber;
	Chamber.bHasId = true;
	Chamber.Id = 1;
	Chamber.CenterCm.X = -400;
	Chamber.CenterCm.Z = 200;
	Chamber.RadiusCm = 200;
	Plan.Chambers.Add(Chamber);
	FVoxelLandM5Junction Junction;
	Junction.bHasId = true;
	Junction.Id = 2;
	Junction.bHasCenter = true;
	Junction.CenterCm.X = -200;
	Junction.CenterCm.Z = 200;
	Plan.Junctions.Add(Junction);
	FVoxelLandM5Entrance Entrance;
	Entrance.bHasId = true;
	Entrance.Id = 3;
	Entrance.bHasConnectionId = true;
	Entrance.ConnectionId = 8;
	Entrance.OpeningBoundsCm.MinInclusive.X = -10;
	Entrance.OpeningBoundsCm.MinInclusive.Y = -10;
	Entrance.OpeningBoundsCm.MinInclusive.Z = 190;
	Entrance.OpeningBoundsCm.MaxExclusive.X = 10;
	Entrance.OpeningBoundsCm.MaxExclusive.Y = 10;
	Entrance.OpeningBoundsCm.MaxExclusive.Z = 210;
	Plan.Entrances.Add(Entrance);
	FVoxelLandM5RouteAnchorChoice A;
	A.Endpoint.bValid = true;
	A.Endpoint.Id = 1;
	A.Endpoint.AnchorRole = EVoxelLandM5AnchorRole::ChamberFeet;
	A.FeetCm.X = -400;
	A.FeetCm.Z = 108;
	FVoxelLandM5RouteAnchorChoice J = A;
	J.Endpoint.Kind = EVoxelLandM5EndpointKind::Junction;
	J.Endpoint.Id = 2;
	J.Endpoint.AnchorRole = EVoxelLandM5AnchorRole::JunctionFeet;
	J.FeetCm.X = -200;
	FVoxelLandM5RouteAnchorChoice Inside = A;
	Inside.Endpoint.Kind = EVoxelLandM5EndpointKind::Entrance;
	Inside.Endpoint.Id = 3;
	Inside.Endpoint.AnchorRole = EVoxelLandM5AnchorRole::EntranceInsideFeet;
	Inside.FeetCm.X = -100;
	FVoxelLandM5RouteAnchorChoice Outside = Inside;
	Outside.Endpoint.AnchorRole = EVoxelLandM5AnchorRole::EntranceOutsideFeet;
	Outside.FeetCm.X = 0;
	FVoxelLandM5Connection C1;
	C1.bHasId = true;
	C1.Id = 7;
	C1.Start = A.Endpoint;
	C1.End = J.Endpoint;
	C1.Start.AnchorRole = EVoxelLandM5AnchorRole::None;
	C1.End.AnchorRole = EVoxelLandM5AnchorRole::None;
	FVoxelLandM5Connection C2;
	C2.bHasId = true;
	C2.Id = 8;
	C2.Start = J.Endpoint;
	C2.End = Outside.Endpoint;
	C2.Start.AnchorRole = EVoxelLandM5AnchorRole::None;
	C2.End.AnchorRole = EVoxelLandM5AnchorRole::None;
	Plan.Connections = {C1, C2};
	for (int32 I = 0; I < 3; ++I)
	{
		FVoxelLandM5Segment S;
		S.bHasId = true;
		S.Id = 9 + I;
		S.bHasConnectionId = true;
		S.ConnectionId = I == 0 ? 7 : 8;
		S.StartCm.X = I == 0 ? -400 : (I == 1 ? -200 : -100);
		S.EndCm.X = I == 0 ? -200 : (I == 1 ? -100 : 0);
		S.StartCm.Z = S.EndCm.Z = 200;
		S.RadiusCm = 103;
		Plan.Segments.Add(S);
	}
	auto Candidate = FVoxelLandM5RouteBuilderFixtureFactory::MakePlan(Settings, Budget.ToSharedRef(), MoveTemp(Plan));
	if (!Candidate.IsValid())
	{
		return false;
	}
	FVoxelLandM5RouteConnectionChoice R1;
	R1.bHasConnectionId = true;
	R1.ConnectionId = 7;
	R1.Start = A.Endpoint;
	R1.End = J.Endpoint;
	R1.SegmentIds = {9};
	FVoxelLandM5RouteConnectionChoice R2;
	R2.bHasConnectionId = true;
	R2.ConnectionId = 8;
	R2.Start = J.Endpoint;
	R2.End = Outside.Endpoint;
	R2.SegmentIds = {10, 11};
	TArray<FVoxelLandM5RouteAnchorChoice> Anchors{Outside, J, A, Inside};
	TArray<FVoxelLandM5RouteConnectionChoice> Choices{R2, R1};
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Result;
	const auto Never = [] {
		return false;
	};
	const auto Error = VoxelLand::M5::RouteBuilder::Build(Settings, 11, Candidate->GetPlan().Region,
		Candidate.ToSharedRef(), Anchors, Choices, Budget.ToSharedRef(), Never, Result);
	if (!TestTrue(TEXT("junction and entrance route proposals"), Error.Code == EVoxelLandM5ErrorCode::None)
		|| !Result.IsValid())
	{
		return false;
	}
	if (!TestEqual(TEXT("two routes"), Result->GetPlan().Routes.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("connection order canonicalized"), Result->GetPlan().Routes[0].Id, uint64(7));
	TestEqual(TEXT("entrance connection second"), Result->GetPlan().Routes[1].Id, uint64(8));
	TestEqual(TEXT("entrance has two continuous legs"), Result->GetPlan().Routes[1].Legs.Num(), 2);
	TestTrue(TEXT("inside and outside anchors differ"),
		Result->GetPlan().Entrances[0].InsideFeetAnchorCm.X != Result->GetPlan().Entrances[0].OutsideFeetAnchorCm.X);
	const auto Structural = VoxelLand::M5::RouteTopology::Validate(
		Settings, 11, Result->GetPlan().Region, Result.ToSharedRef(), Budget.ToSharedRef(), Never);
	TestTrue(TEXT("multi-edge result reaches structural Unproven"), Structural.Code == EVoxelLandM5ErrorCode::Unproven);
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Reordered;
	Swap(Choices[0], Choices[1]);
	Swap(Anchors[0], Anchors[2]);
	const auto Error2 = VoxelLand::M5::RouteBuilder::Build(Settings, 11, Candidate->GetPlan().Region,
		Candidate.ToSharedRef(), Anchors, Choices, Budget.ToSharedRef(), Never, Reordered);
	if (TestTrue(TEXT("reordered input accepted"), Error2.Code == EVoxelLandM5ErrorCode::None)
		&& TestTrue(TEXT("reordered output exists"), Reordered.IsValid() && Reordered->GetPlan().Routes.Num() == 2))
	{
		TestEqual(
			TEXT("first route order is stable"), Reordered->GetPlan().Routes[0].Id, Result->GetPlan().Routes[0].Id);
		TestEqual(
			TEXT("second route order is stable"), Reordered->GetPlan().Routes[1].Id, Result->GetPlan().Routes[1].Id);
		if (TestTrue(TEXT("both entrance legs exist"),
				!Reordered->GetPlan().Routes[1].Legs.IsEmpty() && !Result->GetPlan().Routes[1].Legs.IsEmpty()))
		{
			TestEqual(TEXT("entrance traversal remains continuous"), Reordered->GetPlan().Routes[1].Legs[0].EndFeetCm.X,
				Result->GetPlan().Routes[1].Legs[0].EndFeetCm.X);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5RouteBuilderPortalTest, "VoxelLand.M5.Routes.ExplicitPortalAnchors",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM5RouteBuilderPortalTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings Settings;
	Settings.StageId = FGuid(1, 2, 3, 4);
	Settings.PlanningMode = EVoxelLandM5PlanningMode::Regional;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!FVoxelLandM5Budget::TryCreate(Settings.Limits.MaxBytes, Settings.Limits.MaxValidationWork, Budget)
		|| !Budget.IsValid())
	{
		return false;
	}
	FVoxelLandM5PortalKey Key;
	Key.WorldSeed = 11;
	Key.StageId = Settings.StageId;
	Key.Salt = Settings.Salt;
	Key.RegionA.X = -1;
	Key.RegionB.X = 0;
	Key.Axis = EVoxelLandM5PortalAxis::X;
	TArray<uint8> Digest;
	uint64 Id = 0;
	if (!TestTrue(TEXT("portal digest"), VoxelLand::M5::Seed::TryPortalDigest(11, Settings.StageId,
											 static_cast<uint64>(Settings.Salt), -1, 0, 0, 0, Digest)
											 && VoxelLand::M5::Seed::TryDigestWord(Digest, Id)))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Built[2];
	const auto Never = [] {
		return false;
	};
	for (int32 Side = 0; Side < 2; ++Side)
	{
		FVoxelLandM5CavePlan Plan;
		Plan.Region = Side == 0 ? Key.RegionA : Key.RegionB;
		FVoxelLandM5Chamber Chamber;
		Chamber.bHasId = true;
		Chamber.Id = 1;
		Chamber.CenterCm.X = Side == 0 ? -200 : 200;
		Chamber.CenterCm.Z = 200;
		Chamber.RadiusCm = 200;
		Plan.Chambers.Add(Chamber);
		FVoxelLandM5Portal Portal;
		Portal.bHasId = true;
		Portal.Id = Id;
		Portal.Key = Key;
		Portal.CenterCm.Z = 200;
		Portal.RadiusCm = 103;
		Plan.Portals.Add(Portal);
		FVoxelLandM5Connection Connection;
		Connection.bHasId = true;
		Connection.Id = 7;
		Connection.Start.bValid = true;
		Connection.Start.Id = 1;
		Connection.Start.Region = Plan.Region;
		Connection.End.bValid = true;
		Connection.End.Kind = EVoxelLandM5EndpointKind::Portal;
		Connection.End.Id = Id;
		Connection.End.Region = Plan.Region;
		Plan.Connections.Add(Connection);
		FVoxelLandM5Segment Segment;
		Segment.bHasId = true;
		Segment.Id = 9;
		Segment.bHasConnectionId = true;
		Segment.ConnectionId = 7;
		Segment.StartCm = Chamber.CenterCm;
		Segment.EndCm = Portal.CenterCm;
		Segment.RadiusCm = 103;
		Plan.Segments.Add(Segment);
		auto Candidate =
			FVoxelLandM5RouteBuilderFixtureFactory::MakePlan(Settings, Budget.ToSharedRef(), MoveTemp(Plan));
		if (!TestTrue(TEXT("regional candidate"), Candidate.IsValid()))
		{
			return false;
		}
		FVoxelLandM5RouteAnchorChoice ChamberAnchor;
		ChamberAnchor.Endpoint = Connection.Start;
		ChamberAnchor.Endpoint.AnchorRole = EVoxelLandM5AnchorRole::ChamberFeet;
		ChamberAnchor.FeetCm = Chamber.CenterCm;
		ChamberAnchor.FeetCm.Z = 108;
		FVoxelLandM5RouteAnchorChoice AnchorA;
		AnchorA.Endpoint = Connection.End;
		AnchorA.Endpoint.Region = Key.RegionA;
		AnchorA.Endpoint.AnchorRole = EVoxelLandM5AnchorRole::PortalRegionAFeet;
		AnchorA.FeetCm.Z = 108;
		AnchorA.bHasPortalKey = true;
		AnchorA.PortalKey = Key;
		FVoxelLandM5RouteAnchorChoice AnchorB = AnchorA;
		AnchorB.Endpoint.Region = Key.RegionB;
		AnchorB.Endpoint.AnchorRole = EVoxelLandM5AnchorRole::PortalRegionBFeet;
		TArray<FVoxelLandM5RouteAnchorChoice> Anchors{ChamberAnchor, AnchorB, AnchorA};
		FVoxelLandM5RouteConnectionChoice Choice;
		Choice.bHasConnectionId = true;
		Choice.ConnectionId = 7;
		Choice.Start = ChamberAnchor.Endpoint;
		Choice.End = Side == 0 ? AnchorA.Endpoint : AnchorB.Endpoint;
		Choice.SegmentIds.Add(9);
		TArray<FVoxelLandM5RouteConnectionChoice> Choices{Choice};
		const auto Error = VoxelLand::M5::RouteBuilder::Build(Settings, 11, Candidate->GetPlan().Region,
			Candidate.ToSharedRef(), Anchors, Choices, Budget.ToSharedRef(), Never, Built[Side]);
		if (!TestTrue(TEXT("both portal anchor choices accepted"), Error.Code == EVoxelLandM5ErrorCode::None)
			|| !Built[Side].IsValid())
		{
			return false;
		}
		auto Retained = Built[Side];
		Anchors[1].PortalKey.Salt ^= 1;
		TestTrue(TEXT("portal full-key mismatch rejected"),
			VoxelLand::M5::RouteBuilder::Build(Settings, 11, Candidate->GetPlan().Region, Candidate.ToSharedRef(),
				Anchors, Choices, Budget.ToSharedRef(), Never, Built[Side])
					.Code
				== EVoxelLandM5ErrorCode::InvalidInput);
		TestTrue(TEXT("key failure retains prior result"), Built[Side] == Retained);
		Anchors[1] = AnchorB;
		Anchors.RemoveAt(1);
		TestTrue(TEXT("missing B anchor rejected"),
			VoxelLand::M5::RouteBuilder::Build(Settings, 11, Candidate->GetPlan().Region, Candidate.ToSharedRef(),
				Anchors, Choices, Budget.ToSharedRef(), Never, Built[Side])
					.Code
				!= EVoxelLandM5ErrorCode::None);
		TestTrue(TEXT("missing B anchor retains prior result"), Built[Side] == Retained);
	}
	TestTrue(TEXT("neighbor portal records match handshake comparison"),
		VoxelLand::M5::PortalHandshake::MatchPortalRecords(
			Built[0]->GetPlan().Portals[0], Built[1]->GetPlan().Portals[0]));
	return true;
}
#endif
