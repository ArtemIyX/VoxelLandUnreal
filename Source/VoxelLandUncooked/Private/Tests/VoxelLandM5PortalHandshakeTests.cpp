#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CandidatePlanner.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5PortalHandshake.h"
#include "Generation/VoxelLandM5RouteBuilder.h"
#include "Generation/VoxelLandM5Seed.h"
#include "Misc/AutomationTest.h"

// Test-only admitted candidate copy; it retains the same budget until the
// copied plan, including its route or junction storage, is destroyed.
class FVoxelLandM5HandshakeFixtureFactory
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
constexpr EAutomationTestFlags M5HandshakeFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5PortalHandshakeTest, "VoxelLand.M5.Regional.PortalHandshake", M5HandshakeFlags)
bool FVoxelLandM5PortalHandshakeTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings S;
	S.bEnabled = true;
	S.StageId = FGuid(1, 2, 3, 4);
	S.PlanningMode = EVoxelLandM5PlanningMode::Regional;
	S.Bounds.MinInclusive.X = -200;
	S.Bounds.MaxExclusive.X = 200;
	S.Bounds.MinInclusive.Y = S.Bounds.MinInclusive.Z = -100;
	S.Bounds.MaxExclusive.Y = S.Bounds.MaxExclusive.Z = 100;
	S.RegionSizeCm = 200;
	S.RegionOriginYCm = -100;
	S.Topology.MinChambers = S.Topology.MaxChambers = 1;
	S.Topology.EntranceCount = 0;
	S.Topology.MinLoops = S.Topology.MaxLoops = 0;
	S.Topology.MinTunnelRadiusCm = 1;
	S.Topology.MaxTunnelRadiusCm = 2;
	S.Topology.MinChamberRadiusCm = S.Topology.MaxChamberRadiusCm = 2;
	S.Limits.MaxCandidates = 1;
	S.Limits.MaxEdges = S.Limits.MaxSegments = 32;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("create shared budget"),
			FVoxelLandM5Budget::TryCreate(S.Limits.MaxBytes, S.Limits.MaxValidationWork, Budget)))
	{
		return false;
	}
	FVoxelLandM5RegionCoord Left, Right;
	Left.X = -1;
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> A, B;
	const auto NeverCancel = [] {
		return false;
	};
	const FVoxelLandM5Diagnostic DA =
		VoxelLand::M5::CandidatePlanner::Build(S, 42, Left, Budget.ToSharedRef(), NeverCancel, A);
	const FVoxelLandM5Diagnostic DB =
		VoxelLand::M5::CandidatePlanner::Build(S, 42, Right, Budget.ToSharedRef(), NeverCancel, B);
	if (!TestTrue(TEXT("both regional plans built"), DA.Code == EVoxelLandM5ErrorCode::None
														 && DB.Code == EVoxelLandM5ErrorCode::None && A.IsValid()
														 && B.IsValid())
		|| !TestEqual(TEXT("one shared portal"), A->GetPlan().Portals.Num(), 1))
	{
		return false;
	}
	const FVoxelLandM5Portal& P = A->GetPlan().Portals[0];
	FVoxelLandM5Portal AnchorA = P;
	FVoxelLandM5Portal AnchorB = P;
	AnchorA.bHasRegionAFeetAnchor = AnchorB.bHasRegionAFeetAnchor = true;
	AnchorA.bHasRegionBFeetAnchor = AnchorB.bHasRegionBFeetAnchor = true;
	AnchorA.RegionAFeetAnchorCm.X = AnchorB.RegionAFeetAnchorCm.X = -10;
	AnchorA.RegionBFeetAnchorCm.X = AnchorB.RegionBFeetAnchorCm.X = 10;
	TestTrue(TEXT("matching region-specific anchors agree"),
		VoxelLand::M5::PortalHandshake::MatchPortalRecords(AnchorA, AnchorB));
	AnchorB.RegionAFeetAnchorCm.Z = 1;
	TestFalse(
		TEXT("mismatched A anchor rejected"), VoxelLand::M5::PortalHandshake::MatchPortalRecords(AnchorA, AnchorB));
	AnchorB = AnchorA;
	AnchorB.RegionBFeetAnchorCm.Y = -1;
	TestFalse(
		TEXT("mismatched B anchor rejected"), VoxelLand::M5::PortalHandshake::MatchPortalRecords(AnchorA, AnchorB));
	AnchorB = AnchorA;
	AnchorB.bHasRegionAFeetAnchor = false;
	TestFalse(TEXT("anchor presence mismatch rejected"),
		VoxelLand::M5::PortalHandshake::MatchPortalRecords(AnchorA, AnchorB));
	AnchorA.bHasRegionAFeetAnchor = false;
	TestTrue(TEXT("absent anchors ignore inactive coordinates"),
		VoxelLand::M5::PortalHandshake::MatchPortalRecords(AnchorA, AnchorB));
	TArray<int16> Densities;
	Densities.Init(-20, 343);
	FVoxelLandM5PortalOverlap Block;
	Block.OriginCm.X = P.CenterCm.X - 3;
	Block.OriginCm.Y = P.CenterCm.Y - 3;
	Block.OriginCm.Z = P.CenterCm.Z - 3;
	Block.Counts = FIntVector(7, 7, 7);
	Block.SpacingCm = 1;
	Block.Samples = Densities;
	const TArray<uint8> Identity = {1, 0, 2, 3};
	// A single unanchored route or junction must fail structural validation on
	// either side, and its nested storage remains covered by the fixture lease.
	for (int32 Side = 0; Side < 2; ++Side)
	{
		for (int32 Kind = 0; Kind < 2; ++Kind)
		{
			FVoxelLandM5CavePlan Modified = Side == 0 ? A->GetPlan() : B->GetPlan();
			if (Kind == 0)
			{
				Modified.Junctions.AddDefaulted();
			}
			else
			{
				Modified.Routes.AddDefaulted();
			}
			const auto ModifiedResult =
				FVoxelLandM5HandshakeFixtureFactory::Make(Modified, S, 42, Budget.ToSharedRef());
			if (!TestTrue(TEXT("admit route/junction fixture"), ModifiedResult.IsValid()))
			{
				return false;
			}
			int64 LiveBefore = 0, Peak = 0, WorkBefore = 0;
			Budget->GetUsage(LiveBefore, Peak, WorkBefore);
			int32 Calls = 0;
			const FVoxelLandM5Diagnostic Error = VoxelLand::M5::PortalHandshake::Check(S, 42, Left, Right,
				Side == 0 ? ModifiedResult : A, Side == 1 ? ModifiedResult : B, Identity, Identity, Identity, 1, 1,
				Block, Block, Budget.ToSharedRef(), [&Calls] {
					++Calls;
					return false;
				});
			int64 LiveAfter = 0, WorkAfter = 0;
			Budget->GetUsage(LiveAfter, Peak, WorkAfter);
			TestTrue(TEXT("unanchored route or junction is rejected"), Error.Code != EVoxelLandM5ErrorCode::None);
			TestTrue(TEXT("validation polls cancellation"), Calls > 1);
			TestTrue(TEXT("validation work is charged"), WorkAfter > WorkBefore);
			TestEqual(TEXT("no additional bytes retained"), LiveAfter, LiveBefore);
		}
	}
	const auto Check = [&](TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Other,
						   TConstArrayView<uint8> OtherIdentity, const FVoxelLandM5PortalOverlap& OtherBlock,
						   TFunctionRef<bool()> Cancel) {
		return VoxelLand::M5::PortalHandshake::Check(S, 42, Left, Right, A, Other, Identity, Identity, OtherIdentity, 1,
			1, Block, OtherBlock, Budget.ToSharedRef(), Cancel);
	};
	TestTrue(TEXT("matching overlap and descriptor"),
		Check(B, Identity, Block, NeverCancel).Code == EVoxelLandM5ErrorCode::None);
	const TArray<uint8> DifferentIdentity = {1, 0, 2, 4};
	TestTrue(TEXT("different descriptor rejected"),
		Check(B, DifferentIdentity, Block, NeverCancel).Code != EVoxelLandM5ErrorCode::None);
	TestTrue(TEXT("missing neighbor rejected"),
		Check({}, Identity, Block, NeverCancel).Code == EVoxelLandM5ErrorCode::ProviderUnavailable);
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Foreign;
	const FVoxelLandM5Diagnostic ForeignBuild =
		VoxelLand::M5::CandidatePlanner::Build(S, 43, Right, Budget.ToSharedRef(), NeverCancel, Foreign);
	if (!TestTrue(TEXT("foreign-world candidate built"),
			ForeignBuild.Code == EVoxelLandM5ErrorCode::None && Foreign.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("foreign world rejected despite supplied descriptor"),
		Check(Foreign, Identity, Block, NeverCancel).Code == EVoxelLandM5ErrorCode::InvalidInput);
	TArray<int16> DifferentDensity = Densities;
	DifferentDensity[171] = 1;
	FVoxelLandM5PortalOverlap Changed = Block;
	Changed.Samples = DifferentDensity;
	TestTrue(TEXT("different face density rejected"),
		Check(B, Identity, Changed, NeverCancel).Code == EVoxelLandM5ErrorCode::InconsistentTopology);
	Changed = Block;
	Changed.OriginCm.X += 1;
	TestTrue(TEXT("different global overlap rejected"),
		Check(B, Identity, Changed, NeverCancel).Code == EVoxelLandM5ErrorCode::OutOfDomain);
	Changed = Block;
	Changed.Counts.X = 6;
	TestTrue(TEXT("incomplete sample buffer rejected"),
		Check(B, Identity, Changed, NeverCancel).Code == EVoxelLandM5ErrorCode::OutOfDomain);
	TestTrue(TEXT("cancelled immediately"),
		Check(B, Identity, Block, [] { return true; }).Code == EVoxelLandM5ErrorCode::Cancelled);
	Changed = Block;
	Changed.SpacingCm = 2;
	TestTrue(TEXT("coarse grid rejected despite equal samples"),
		Check(B, Identity, Changed, NeverCancel).Code == EVoxelLandM5ErrorCode::OutOfDomain);
	FVoxelLandM5PortalOverlap Shifted = Block;
	Shifted.SpacingCm = 2;
	const FVoxelLandM5Diagnostic ShiftedResult = VoxelLand::M5::PortalHandshake::Check(S, 42, Left, Right, A, B,
		Identity, Identity, Identity, 2, 1, Shifted, Shifted, Budget.ToSharedRef(), NeverCancel);
	TestTrue(TEXT("both equally shifted grids are rejected"), ShiftedResult.Code == EVoxelLandM5ErrorCode::OutOfDomain);
	Shifted.OriginCm.X = -4;
	Shifted.OriginCm.Y = -3;
	Shifted.OriginCm.Z = -4;
	const FVoxelLandM5Diagnostic TangentialShift = VoxelLand::M5::PortalHandshake::Check(S, 42, Left, Right, A, B,
		Identity, Identity, Identity, 2, 1, Shifted, Shifted, Budget.ToSharedRef(), NeverCancel);
	TestTrue(TEXT("tangential shift is rejected even with face-axis alignment"),
		TangentialShift.Code == EVoxelLandM5ErrorCode::OutOfDomain);
	const FVoxelLandM5Diagnostic MissingHalo = VoxelLand::M5::PortalHandshake::Check(
		S, 42, Left, Right, A, B, Identity, Identity, Identity, 1, 2, Block, Block, Budget.ToSharedRef(), NeverCancel);
	TestTrue(TEXT("missing required halo rejected"), MissingHalo.Code == EVoxelLandM5ErrorCode::OutOfDomain);
	int64 LiveBefore = 0, Peak = 0, Work = 0;
	Budget->GetUsage(LiveBefore, Peak, Work);
	int32 CancelCalls = 0;
	TestTrue(TEXT("cancellation after admission is reported"), Check(B, Identity, Block, [&CancelCalls] {
		return ++CancelCalls == 2;
	}).Code == EVoxelLandM5ErrorCode::Cancelled);
	int64 LiveAfter = 0;
	Budget->GetUsage(LiveAfter, Peak, Work);
	TestEqual(TEXT("cancelled handshake releases digest lease"), LiveAfter, LiveBefore);
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Occupied;
	if (!TestTrue(TEXT("reserve remaining bytes"), Budget->TryReserveBytes(S.Limits.MaxBytes - LiveBefore, Occupied)))
	{
		return false;
	}
	TestTrue(TEXT("digest scratch admission fails under memory exhaustion"),
		Check(B, Identity, Block, NeverCancel).Code == EVoxelLandM5ErrorCode::BudgetExceeded);
	Occupied.Reset();
	Budget->GetUsage(LiveAfter, Peak, Work);
	TestEqual(TEXT("failed handshake preserves prior live reservations"), LiveAfter, LiveBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5PortalHandshakeWorkTest, "VoxelLand.M5.Regional.PortalHandshakeWork", M5HandshakeFlags)
bool FVoxelLandM5PortalHandshakeWorkTest::RunTest(const FString& Parameters)
{
	// Independent upper-bound arithmetic for this fixture:
	// candidate = 1024 + 8*(4*129+5+8) + 8*10^2 + 10*42
	//             + 12*129 + 8*5^2 = 8224, twice = 16448.
	// handshake = 343 sample comparisons + 8 descriptor comparisons + 4
	// portal operations + 7 cancellation polls + 8192 hash allowance = 8554.
	// Each topology check = 16*(2 nodes+1 edge+1)^2 + 1024 + 4096 = 5376.
	// Each candidate also encodes one chamber, segment, connection and portal:
	// 1 inventory + 2*(128+128+160+128+192) = 1473 work per candidate.
	// Total: 16448 + 2*1473 + 8554 + 2*5376 = 38700.
	constexpr int64 RequiredWork = 38700;
	for (int32 Short = 0; Short <= 1; ++Short)
	{
		FVoxelLandM5CaveSettings S;
		S.bEnabled = true;
		S.StageId = FGuid(1, 2, 3, 4);
		S.PlanningMode = EVoxelLandM5PlanningMode::Regional;
		S.Bounds.MinInclusive.X = -200;
		S.Bounds.MaxExclusive.X = 200;
		S.Bounds.MinInclusive.Y = S.Bounds.MinInclusive.Z = -100;
		S.Bounds.MaxExclusive.Y = S.Bounds.MaxExclusive.Z = 100;
		S.RegionSizeCm = 200;
		S.RegionOriginYCm = -100;
		S.Topology.MinChambers = S.Topology.MaxChambers = 1;
		S.Topology.EntranceCount = 0;
		S.Topology.MinLoops = S.Topology.MaxLoops = 0;
		S.Topology.MinTunnelRadiusCm = 1;
		S.Topology.MaxTunnelRadiusCm = 2;
		S.Topology.MinChamberRadiusCm = S.Topology.MaxChamberRadiusCm = 2;
		S.Limits.MaxCandidates = 1;
		S.Limits.MaxEdges = S.Limits.MaxSegments = 32;
		S.Limits.MaxValidationWork = RequiredWork - Short;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
		if (!TestTrue(TEXT("create exact budget"),
				FVoxelLandM5Budget::TryCreate(S.Limits.MaxBytes, S.Limits.MaxValidationWork, Budget)))
		{
			return false;
		}
		FVoxelLandM5RegionCoord Left, Right;
		Left.X = -1;
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> A, B;
		const auto NeverCancel = [] {
			return false;
		};
		const auto DA = VoxelLand::M5::CandidatePlanner::Build(S, 42, Left, Budget.ToSharedRef(), NeverCancel, A);
		const auto DB = VoxelLand::M5::CandidatePlanner::Build(S, 42, Right, Budget.ToSharedRef(), NeverCancel, B);
		if (!TestTrue(TEXT("admit both candidates"), DA.Code == EVoxelLandM5ErrorCode::None
														 && DB.Code == EVoxelLandM5ErrorCode::None && A.IsValid()
														 && B.IsValid())
			|| !TestEqual(TEXT("one portal for fixture"), A->GetPlan().Portals.Num(), 1))
		{
			return false;
		}
		int64 LiveBefore = 0, Peak = 0, Work = 0;
		Budget->GetUsage(LiveBefore, Peak, Work);
		TestEqual(TEXT("both candidate precharges"), Work, int64(19394));
		const FVoxelLandM5Portal& Portal = A->GetPlan().Portals[0];
		TArray<int16> Samples;
		Samples.Init(-20, 343);
		FVoxelLandM5PortalOverlap Block;
		Block.OriginCm.X = Portal.CenterCm.X - 3;
		Block.OriginCm.Y = Portal.CenterCm.Y - 3;
		Block.OriginCm.Z = Portal.CenterCm.Z - 3;
		Block.Counts = FIntVector(7, 7, 7);
		Block.SpacingCm = 1;
		Block.Samples = Samples;
		const TArray<uint8> Identity = {1, 0, 2, 3};
		const FVoxelLandM5Diagnostic Result = VoxelLand::M5::PortalHandshake::Check(S, 42, Left, Right, A, B, Identity,
			Identity, Identity, 1, 1, Block, Block, Budget.ToSharedRef(), NeverCancel);
		TestTrue(TEXT("exact limit succeeds and one below fails"),
			Result.Code == (Short == 0 ? EVoxelLandM5ErrorCode::None : EVoxelLandM5ErrorCode::BudgetExceeded));
		int64 LiveAfter = 0;
		Budget->GetUsage(LiveAfter, Peak, Work);
		TestEqual(TEXT("scratch leases released without replacing plan handles"), LiveAfter, LiveBefore);
		TestTrue(TEXT("candidate handles remain valid"), A.IsValid() && B.IsValid());
		TestEqual(TEXT("cumulative precharge at boundary"), Work, Short == 0 ? RequiredWork : int64(33324));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5RoutedPortalHandshakeTest, "VoxelLand.M5.Regional.RoutedPortalHandshake", M5HandshakeFlags)
bool FVoxelLandM5RoutedPortalHandshakeTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings S;
	S.bEnabled = true;
	S.StageId = FGuid(1, 2, 3, 4);
	S.PlanningMode = EVoxelLandM5PlanningMode::Regional;
	S.RegionSizeCm = 1000;
	S.Topology.MinChambers = S.Topology.MaxChambers = 1;
	S.Topology.EntranceCount = 0;
	S.Topology.MinLoops = S.Topology.MaxLoops = 0;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!FVoxelLandM5Budget::TryCreate(S.Limits.MaxBytes, S.Limits.MaxValidationWork, Budget))
	{
		return false;
	}
	FVoxelLandM5PortalKey Key;
	Key.WorldSeed = 11;
	Key.StageId = S.StageId;
	Key.RegionA.X = -1;
	TArray<uint8> Digest;
	uint64 PortalId = 0;
	if (!TestTrue(TEXT("independent shared portal identifier"),
			VoxelLand::M5::Seed::TryPortalDigest(11, S.StageId, static_cast<uint64>(S.Salt), -1, 0, 0, 0, Digest)
				&& VoxelLand::M5::Seed::TryDigestWord(Digest, PortalId)))
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
		Portal.Id = PortalId;
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
		Connection.End.Id = PortalId;
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
		const auto Candidate = FVoxelLandM5HandshakeFixtureFactory::Make(Plan, S, 11, Budget.ToSharedRef());
		if (!TestTrue(TEXT("admit regional candidate"), Candidate.IsValid()))
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
		TArray<FVoxelLandM5RouteAnchorChoice> Anchors{ChamberAnchor, AnchorA, AnchorB};
		FVoxelLandM5RouteConnectionChoice Choice;
		Choice.bHasConnectionId = true;
		Choice.ConnectionId = 7;
		Choice.Start = ChamberAnchor.Endpoint;
		Choice.End = Side == 0 ? AnchorA.Endpoint : AnchorB.Endpoint;
		Choice.SegmentIds.Add(9);
		const TArray<FVoxelLandM5RouteConnectionChoice> Choices{Choice};
		const FVoxelLandM5Diagnostic Result = VoxelLand::M5::RouteBuilder::Build(
			S, 11, Plan.Region, Candidate.ToSharedRef(), Anchors, Choices, Budget.ToSharedRef(), Never, Built[Side]);
		if (!TestTrue(TEXT("build route using real route builder"),
				Result.Code == EVoxelLandM5ErrorCode::None && Built[Side].IsValid()))
		{
			return false;
		}
	}
	TArray<int16> Samples;
	Samples.Init(-10, 343);
	FVoxelLandM5PortalOverlap Block;
	Block.OriginCm.X = -300;
	Block.OriginCm.Y = -300;
	Block.OriginCm.Z = -100;
	Block.Counts = FIntVector(7, 7, 7);
	Block.SpacingCm = 100;
	Block.Samples = Samples;
	const TArray<uint8> Identity = {5, 1, 0, 8};
	const auto Check = [&](TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Other,
						   TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> UseBudget) {
		return VoxelLand::M5::PortalHandshake::Check(S, 11, Key.RegionA, Key.RegionB, Built[0], Other, Identity,
			Identity, Identity, 100, 1, Block, Block, UseBudget, Never);
	};
	TestTrue(TEXT("structurally valid routed regions agree"),
		Check(Built[1], Budget.ToSharedRef()).Code == EVoxelLandM5ErrorCode::None);
	FVoxelLandM5CavePlan Invalid = Built[1]->GetPlan();
	Invalid.Portals[0].RegionBFeetAnchorCm.Z += 1;
	const auto Mismatched = FVoxelLandM5HandshakeFixtureFactory::Make(Invalid, S, 11, Budget.ToSharedRef());
	if (!TestTrue(TEXT("admit inconsistent anchor fixture"), Mismatched.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("different route anchor rejected"),
		Check(Mismatched, Budget.ToSharedRef()).Code != EVoxelLandM5ErrorCode::None);
	Invalid = Built[1]->GetPlan();
	if (!TestTrue(TEXT("route and leg exist"), !Invalid.Routes.IsEmpty() && !Invalid.Routes[0].Legs.IsEmpty()))
	{
		return false;
	}
	Invalid.Routes[0].Legs[0].EndFeetCm.Z += 1;
	const auto BrokenRoute = FVoxelLandM5HandshakeFixtureFactory::Make(Invalid, S, 11, Budget.ToSharedRef());
	if (!TestTrue(TEXT("admit invalid route fixture"), BrokenRoute.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("structurally invalid route rejected"),
		Check(BrokenRoute, Budget.ToSharedRef()).Code != EVoxelLandM5ErrorCode::None);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> ForeignBudget;
	if (!FVoxelLandM5Budget::TryCreate(S.Limits.MaxBytes, S.Limits.MaxValidationWork, ForeignBudget))
	{
		return false;
	}
	TestTrue(TEXT("foreign budget rejected"),
		Check(Built[1], ForeignBudget.ToSharedRef()).Code == EVoxelLandM5ErrorCode::InvalidInput);
	int64 LiveBefore = 0, Peak = 0, Work = 0;
	Budget->GetUsage(LiveBefore, Peak, Work);
	if (!TestTrue(TEXT("exhaust remaining shared work"), Budget->TryChargeWork(S.Limits.MaxValidationWork - Work)))
	{
		return false;
	}
	TestTrue(TEXT("routed handshake budget exhaustion is reported"),
		Check(Built[1], Budget.ToSharedRef()).Code == EVoxelLandM5ErrorCode::BudgetExceeded);
	int64 LiveAfter = 0;
	Budget->GetUsage(LiveAfter, Peak, Work);
	TestEqual(TEXT("budget failure leaves candidate reservations intact"), LiveAfter, LiveBefore);
	return true;
}
#endif
