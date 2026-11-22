#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5Topology.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Misc/AutomationTest.h"

// Test-only construction still charges the same shared budget before copying
// the fixture. Production candidates can only be produced by planner APIs.
class FVoxelLandM5TopologyFixtureFactory
{
public:
	static TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Make(const FVoxelLandM5CavePlan& Plan,
		const FVoxelLandM5CaveSettings& Settings, int64 Seed,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget)
	{
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
		if (!Budget->TryReserveBytes(65536, Lease))
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
constexpr EAutomationTestFlags M5TopologyFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
const FGuid M5TopologyStage(0x01234567, 0x89abcdef, 0x10203040, 0xfedcba98);
constexpr int64 M5TopologySeed = -1;
constexpr int64 M5TopologySalt = 0x0102030405060708LL;

FVoxelLandM5Diagnostic M5TopologyValidate(const FVoxelLandM5CavePlan& Plan, int32 MaxChambers, int32 MaxConnections,
	TFunctionRef<bool()> IsCancelled, int64 ValidationSeed = M5TopologySeed, FGuid ValidationStage = M5TopologyStage,
	int64 ValidationSalt = M5TopologySalt)
{
	FVoxelLandM5CaveSettings Settings;
	Settings.StageId = M5TopologyStage;
	Settings.Salt = M5TopologySalt;
	Settings.Topology.EntranceCount = 1;
	Settings.Limits.MaxCandidates = MaxChambers;
	Settings.Limits.MaxEdges = MaxConnections;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!FVoxelLandM5Budget::TryCreate(Settings.Limits.MaxBytes, Settings.Limits.MaxValidationWork, Budget))
	{
		FVoxelLandM5Diagnostic Error;
		Error.Code = EVoxelLandM5ErrorCode::InvalidInput;
		return Error;
	}
	auto Candidate = FVoxelLandM5TopologyFixtureFactory::Make(Plan, Settings, M5TopologySeed, Budget.ToSharedRef());
	if (!Candidate.IsValid())
	{
		FVoxelLandM5Diagnostic Error;
		Error.Code = EVoxelLandM5ErrorCode::InvalidInput;
		return Error;
	}
	Settings.StageId = ValidationStage;
	Settings.Salt = ValidationSalt;
	return VoxelLand::M5::Topology::Validate(
		Settings, ValidationSeed, Plan.Region, Candidate.ToSharedRef(), Budget.ToSharedRef(), IsCancelled);
}

FVoxelLandM5Endpoint M5TopologyEndpoint(EVoxelLandM5EndpointKind Kind, uint64 Id, int64 X = -2, int64 Y = 4)
{
	FVoxelLandM5Endpoint Endpoint;
	Endpoint.bValid = true;
	Endpoint.Kind = Kind;
	Endpoint.Id = Id;
	Endpoint.Region.X = X;
	Endpoint.Region.Y = Y;
	return Endpoint;
}

FVoxelLandM5Connection M5TopologyLink(uint64 Id, const FVoxelLandM5Endpoint& Start, const FVoxelLandM5Endpoint& End)
{
	FVoxelLandM5Connection Connection;
	Connection.bHasId = true;
	Connection.Id = Id;
	Connection.Start = Start;
	Connection.End = End;
	return Connection;
}

FVoxelLandM5CavePlan M5TopologyFixture()
{
	FVoxelLandM5CavePlan Plan;
	Plan.Region.X = -2;
	Plan.Region.Y = 4;
	FVoxelLandM5Chamber A;
	A.bHasId = true;
	A.Id = 0; // Zero is a valid digest-derived ID.
	Plan.Chambers.Add(A);
	FVoxelLandM5Chamber B;
	B.bHasId = true;
	B.Id = 8;
	Plan.Chambers.Add(B);
	FVoxelLandM5Entrance Entrance;
	Entrance.bHasId = true;
	Entrance.Id = 0; // Different node kinds have independent ID domains.
	Entrance.bHasConnectionId = true;
	Entrance.ConnectionId = 2;
	Plan.Entrances.Add(Entrance);
	FVoxelLandM5Portal Portal;
	Portal.bHasId = true;
	// Independently calculated reference vector in M5_Numeric_Contract_Proposal.md.
	Portal.Id = 0x6381b9955e50ad8dULL;
	Portal.Key.WorldSeed = M5TopologySeed;
	Portal.Key.StageId = M5TopologyStage;
	Portal.Key.Salt = M5TopologySalt;
	Portal.Key.RegionA.X = -2;
	Portal.Key.RegionA.Y = 4;
	Portal.Key.RegionB.X = -1;
	Portal.Key.RegionB.Y = 4;
	Portal.Key.Axis = EVoxelLandM5PortalAxis::X;
	Plan.Portals.Add(Portal);
	const auto ANode = M5TopologyEndpoint(EVoxelLandM5EndpointKind::Chamber, 0);
	const auto BNode = M5TopologyEndpoint(EVoxelLandM5EndpointKind::Chamber, 8);
	const auto ENode = M5TopologyEndpoint(EVoxelLandM5EndpointKind::Entrance, 0);
	const auto PNode = M5TopologyEndpoint(EVoxelLandM5EndpointKind::Portal, Portal.Id);
	Plan.Connections.Add(M5TopologyLink(1, ANode, BNode));
	Plan.Connections.Add(M5TopologyLink(2, ANode, ENode));
	Plan.Connections.Add(M5TopologyLink(3, BNode, PNode));
	return Plan;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5TopologyGraphTest, "VoxelLand.M5.Topology.Graph", M5TopologyFlags)
bool FVoxelLandM5TopologyGraphTest::RunTest(const FString& Parameters)
{
	const auto NeverCancel = []() {
		return false;
	};
	auto Plan = M5TopologyFixture();
	auto Validate = [&NeverCancel](const FVoxelLandM5CavePlan& Candidate) {
		return M5TopologyValidate(Candidate, 32, 32, NeverCancel).Code;
	};
	TestTrue(TEXT("connected graph, zero IDs and independent portal vector accepted"),
		Validate(Plan) == EVoxelLandM5ErrorCode::None);
	Plan.Connections.Add(M5TopologyLink(4, M5TopologyEndpoint(EVoxelLandM5EndpointKind::Chamber, 0),
		M5TopologyEndpoint(EVoxelLandM5EndpointKind::Portal, Plan.Portals[0].Id)));
	TestTrue(TEXT("connected cycle accepted"), Validate(Plan) == EVoxelLandM5ErrorCode::None);
	Plan.Connections.RemoveAt(3);
	Plan.Connections.RemoveAt(2);
	TestTrue(TEXT("isolated portal rejected"), Validate(Plan) == EVoxelLandM5ErrorCode::InconsistentTopology);
	Plan = M5TopologyFixture();
	Plan.Connections.RemoveAt(1);
	TestTrue(TEXT("isolated entrance rejected"), Validate(Plan) == EVoxelLandM5ErrorCode::InconsistentTopology);
	Plan = M5TopologyFixture();
	Plan.Chambers.Add(Plan.Chambers[0]);
	TestTrue(TEXT("duplicate typed node rejected"), Validate(Plan) == EVoxelLandM5ErrorCode::InconsistentTopology);
	Plan = M5TopologyFixture();
	Plan.Connections[0].End = Plan.Connections[0].Start;
	TestTrue(TEXT("self edge rejected"), Validate(Plan) == EVoxelLandM5ErrorCode::InconsistentTopology);
	Plan = M5TopologyFixture();
	Plan.Connections.Add(M5TopologyLink(4, Plan.Connections[0].End, Plan.Connections[0].Start));
	TestTrue(TEXT("reversed duplicate edge rejected"), Validate(Plan) == EVoxelLandM5ErrorCode::InconsistentTopology);
	Plan = M5TopologyFixture();
	Plan.Connections[0].Start.Region.X++;
	TestTrue(TEXT("wrong endpoint region rejected"), Validate(Plan) == EVoxelLandM5ErrorCode::InvalidInput);
	Plan = M5TopologyFixture();
	Plan.Connections[1].Id = 10;
	TestTrue(
		TEXT("entrance connection reference checked"), Validate(Plan) == EVoxelLandM5ErrorCode::InconsistentTopology);
	Plan = M5TopologyFixture();
	Plan.Portals[0].Key.Axis = EVoxelLandM5PortalAxis::Y;
	TestTrue(TEXT("mismatched portal axis rejected"), Validate(Plan) == EVoxelLandM5ErrorCode::InvalidInput);
	Plan = M5TopologyFixture();
	Plan.Portals[0].Id ^= 1;
	TestTrue(TEXT("mismatched portal digest rejected"), Validate(Plan) == EVoxelLandM5ErrorCode::InvalidInput);
	Plan = M5TopologyFixture();
	Plan.Chambers[0].bHasId = false;
	TestTrue(TEXT("missing ID rejected even though its value is zero"),
		Validate(Plan) == EVoxelLandM5ErrorCode::InvalidInput);
	Plan = M5TopologyFixture();
	Plan.Chambers.Empty();
	TestTrue(TEXT("chamberless graph rejected"), Validate(Plan) == EVoxelLandM5ErrorCode::InconsistentTopology);
	Plan = M5TopologyFixture();
	Plan.Connections.Empty();
	TestTrue(TEXT("graph without routes rejected"), Validate(Plan) == EVoxelLandM5ErrorCode::InconsistentTopology);
	Plan = M5TopologyFixture();
	TestTrue(TEXT("self-consistent portal from another world rejected"),
		M5TopologyValidate(Plan, 32, 32, NeverCancel, 7).Code == EVoxelLandM5ErrorCode::InvalidInput);
	TestTrue(TEXT("portal from another stage rejected"),
		M5TopologyValidate(Plan, 32, 32, NeverCancel, M5TopologySeed, FGuid(1, 2, 3, 4)).Code
			== EVoxelLandM5ErrorCode::InvalidInput);
	TestTrue(TEXT("portal from another salt rejected"),
		M5TopologyValidate(Plan, 32, 32, NeverCancel, M5TopologySeed, M5TopologyStage, 0).Code
			== EVoxelLandM5ErrorCode::InvalidInput);
	// Independent SHA-256 of the documented portal packet with world seed 7:
	// 38dd37de322af458ff634b1220d296620a07d8315f112c9ec5935be98c9ec8c4.
	FVoxelLandM5Portal OtherWorld = Plan.Portals[0];
	OtherWorld.Key.WorldSeed = 7;
	OtherWorld.Id = 0x58f42a32de37dd38ULL;
	Plan.Portals.Add(OtherWorld);
	TestTrue(TEXT("mixed self-consistent portal world keys rejected"),
		Validate(Plan) == EVoxelLandM5ErrorCode::InvalidInput);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5TopologyBudgetTest, "VoxelLand.M5.Topology.SharedBudget", M5TopologyFlags)
bool FVoxelLandM5TopologyBudgetTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings Settings;
	Settings.StageId = M5TopologyStage;
	Settings.Salt = M5TopologySalt;
	Settings.Topology.EntranceCount = 1;
	Settings.Limits.MaxValidationWork = 10000;
	const FVoxelLandM5CavePlan Plan = M5TopologyFixture();
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"), FVoxelLandM5Budget::TryCreate(Settings.Limits.MaxBytes, 10000, Budget)))
	{
		return false;
	}
	const auto Candidate =
		FVoxelLandM5TopologyFixtureFactory::Make(Plan, Settings, M5TopologySeed, Budget.ToSharedRef());
	if (!TestTrue(TEXT("candidate fixture admitted"), Candidate.IsValid()))
	{
		return false;
	}
	const auto NeverCancel = []() {
		return false;
	};
	const auto Validate = [&]() {
		return VoxelLand::M5::Topology::Validate(
			Settings, M5TopologySeed, Plan.Region, Candidate.ToSharedRef(), Budget.ToSharedRef(), NeverCancel);
	};
	TestTrue(TEXT("first topology admission succeeds"), Validate().Code == EVoxelLandM5ErrorCode::None);
	int64 Live = 0, Peak = 0, Work = 0;
	Budget->GetUsage(Live, Peak, Work);
	const int64 ExpectedScratch = 4096
								+ 2
									  * (4 * (sizeof(FVoxelLandM5Endpoint) + sizeof(int32))
										  + 3 * (sizeof(FVoxelLandM5Connection) + sizeof(uint64)));
	TestEqual(TEXT("scratch lease released"), Live, int64(65536));
	TestEqual(TEXT("peak includes candidate and full scratch"), Peak, int64(65536) + ExpectedScratch);
	// 16*(4 nodes + 3 edges + 1)^2 + 1024 per portal + 4096.
	TestEqual(TEXT("independent work admission"), Work, int64(6144));
	TestTrue(TEXT("cumulative work rejects second attempt"), Validate().Code == EVoxelLandM5ErrorCode::BudgetExceeded);
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("failed retry releases scratch"), Live, int64(65536));
	TestEqual(TEXT("rejected work leaves cumulative charge unchanged"), Work, int64(6144));
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> OtherBudget;
	if (!TestTrue(
			TEXT("second budget created"), FVoxelLandM5Budget::TryCreate(Settings.Limits.MaxBytes, 10000, OtherBudget)))
	{
		return false;
	}
	TestTrue(TEXT("candidate cannot switch budgets"),
		VoxelLand::M5::Topology::Validate(
			Settings, M5TopologySeed, Plan.Region, Candidate.ToSharedRef(), OtherBudget.ToSharedRef(), NeverCancel)
				.Code
			== EVoxelLandM5ErrorCode::InvalidInput);
	const auto CancelCandidate =
		FVoxelLandM5TopologyFixtureFactory::Make(Plan, Settings, M5TopologySeed, OtherBudget.ToSharedRef());
	if (!TestTrue(TEXT("cancellation fixture admitted"), CancelCandidate.IsValid()))
	{
		return false;
	}
	int32 CancelCalls = 0;
	const auto CancelAfterAdmission = [&CancelCalls]() {
		return ++CancelCalls == 2;
	};
	TestTrue(TEXT("cancellation after admission reported"),
		VoxelLand::M5::Topology::Validate(Settings, M5TopologySeed, Plan.Region, CancelCandidate.ToSharedRef(),
			OtherBudget.ToSharedRef(), CancelAfterAdmission)
				.Code
			== EVoxelLandM5ErrorCode::Cancelled);
	OtherBudget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("cancel releases temporary scratch"), Live, int64(65536));
	TestEqual(TEXT("cancel retains precharged work"), Work, int64(6144));
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Oversized;
	if (!TestTrue(TEXT("oversized budget created"),
			FVoxelLandM5Budget::TryCreate(Settings.Limits.MaxBytes, 10001, Oversized)))
	{
		return false;
	}
	const auto OversizedCandidate =
		FVoxelLandM5TopologyFixtureFactory::Make(Plan, Settings, M5TopologySeed, Oversized.ToSharedRef());
	if (!TestTrue(TEXT("oversized fixture admitted"), OversizedCandidate.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("budget above authored cap rejected"),
		VoxelLand::M5::Topology::Validate(Settings, M5TopologySeed, Plan.Region, OversizedCandidate.ToSharedRef(),
			Oversized.ToSharedRef(), NeverCancel)
				.Code
			== EVoxelLandM5ErrorCode::InvalidInput);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5TopologyAdmissionTest, "VoxelLand.M5.Topology.AdmissionAndCancellation", M5TopologyFlags)
bool FVoxelLandM5TopologyAdmissionTest::RunTest(const FString& Parameters)
{
	auto Plan = M5TopologyFixture();
	const auto NeverCancel = []() {
		return false;
	};
	const auto Failure = M5TopologyValidate(Plan, 1, 3, NeverCancel);
	TestTrue(TEXT("node budget checked before graph creation"), Failure.Code == EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("connection budget checked"),
		M5TopologyValidate(Plan, 4, 2, NeverCancel).Code == EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(
		TEXT("exact limits accepted"), M5TopologyValidate(Plan, 4, 3, NeverCancel).Code == EVoxelLandM5ErrorCode::None);
	int32 Calls = 0;
	const auto CancelInside = [&Calls]() {
		return ++Calls == 5;
	};
	TestTrue(TEXT("cancellation during node scan reported"),
		M5TopologyValidate(Plan, 4, 3, CancelInside).Code == EVoxelLandM5ErrorCode::Cancelled);
	int32 TotalCalls = 0;
	const auto CountCalls = [&TotalCalls]() {
		++TotalCalls;
		return false;
	};
	TestTrue(TEXT("counting validation succeeds"),
		M5TopologyValidate(Plan, 4, 3, CountCalls).Code == EVoxelLandM5ErrorCode::None);
	int32 FinalCalls = 0;
	const auto CancelAtPublication = [&FinalCalls, TotalCalls]() {
		return ++FinalCalls == TotalCalls;
	};
	TestTrue(TEXT("cancellation at final publication check is observed"),
		M5TopologyValidate(Plan, 4, 3, CancelAtPublication).Code == EVoxelLandM5ErrorCode::Cancelled);
	TestEqual(TEXT("reached same final check"), FinalCalls, TotalCalls);
	TestEqual(TEXT("input remains intact after failure"), Plan.Connections.Num(), 3);
	TestEqual(TEXT("input portal remains intact"), Plan.Portals[0].Id, uint64(0x6381b9955e50ad8dULL));
	return true;
}
#endif
