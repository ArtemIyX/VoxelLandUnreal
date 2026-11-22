#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5CandidatePlanner.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5CandidateFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
FVoxelLandM5CaveSettings M5CandidateSettings()
{
	FVoxelLandM5CaveSettings S;
	S.bEnabled = true;
	S.StageId = FGuid(1, 2, 3, 4);
	S.Bounds.MinInclusive.X = -100;
	S.Bounds.MinInclusive.Y = -100;
	S.Bounds.MinInclusive.Z = -100;
	S.Bounds.MaxExclusive.X = 100;
	S.Bounds.MaxExclusive.Y = 100;
	S.Bounds.MaxExclusive.Z = 100;
	S.Topology.MinChambers = 3;
	S.Topology.MaxChambers = 3;
	S.Topology.MinLoops = 0;
	S.Topology.MaxLoops = 0;
	S.Topology.MinTunnelRadiusCm = 1;
	S.Topology.MaxTunnelRadiusCm = 1;
	S.Topology.MinChamberRadiusCm = 2;
	S.Topology.MaxChamberRadiusCm = 2;
	S.Limits.MaxCandidates = 3;
	S.Limits.MaxEdges = 32;
	S.Limits.MaxSegments = 32;
	return S;
}
bool M5CandidateSuccess(const FVoxelLandM5Diagnostic& D)
{
	return D.Code == EVoxelLandM5ErrorCode::None;
}
using FM5CandidateHandle = TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>;
FM5CandidateHandle M5CandidateSentinel()
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	FM5CandidateHandle Out;
	if (!FVoxelLandM5Budget::TryCreate(
			M5CandidateSettings().Limits.MaxBytes, M5CandidateSettings().Limits.MaxValidationWork, Budget))
	{
		return {};
	}
	const FVoxelLandM5Diagnostic Error = VoxelLand::M5::CandidatePlanner::Build(
		M5CandidateSettings(), 42, {}, Budget.ToSharedRef(), [] { return false; }, Out);
	return M5CandidateSuccess(Error) ? Out : FM5CandidateHandle();
}
FVoxelLandM5Diagnostic M5CandidateBuild(const FVoxelLandM5CaveSettings& S, int64 Seed,
	const FVoxelLandM5RegionCoord& Region, TFunctionRef<bool()> Cancel, FM5CandidateHandle& Out)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!FVoxelLandM5Budget::TryCreate(S.Limits.MaxBytes, S.Limits.MaxValidationWork, Budget))
	{
		FVoxelLandM5Diagnostic Error;
		Error.Code = EVoxelLandM5ErrorCode::InvalidInput;
		return Error;
	}
	return VoxelLand::M5::CandidatePlanner::Build(S, Seed, Region, Budget.ToSharedRef(), Cancel, Out);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5CandidateFiniteTest, "VoxelLand.M5.Candidates.FiniteReference", M5CandidateFlags)
bool FVoxelLandM5CandidateFiniteTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings S = M5CandidateSettings();
	FM5CandidateHandle Out;
	const FVoxelLandM5Diagnostic Error = M5CandidateBuild(S, 42, {}, [] { return false; }, Out);
	if (!TestTrue(TEXT("finite candidate succeeds"), M5CandidateSuccess(Error)))
	{
		return false;
	}
	if (!TestEqual(TEXT("three accepted chambers"), Out->GetPlan().Chambers.Num(), 3)
		|| !TestEqual(TEXT("two spanning tree edges"), Out->GetPlan().Connections.Num(), 2))
	{
		return false;
	}
	// Independently calculated from Python hashlib SHA-256 and the documented packet.
	const int64 Expected[3][3] = {{23, -82, 95}, {-29, 91, -72}, {53, 96, 15}};
	for (int32 I = 0; I < 3; ++I)
	{
		TestEqual(TEXT("candidate X"), Out->GetPlan().Chambers[I].CenterCm.X, Expected[I][0]);
		TestEqual(TEXT("candidate Y"), Out->GetPlan().Chambers[I].CenterCm.Y, Expected[I][1]);
		TestEqual(TEXT("candidate Z"), Out->GetPlan().Chambers[I].CenterCm.Z, Expected[I][2]);
		TestEqual(TEXT("stable candidate ID"), Out->GetPlan().Chambers[I].Id, uint64(I));
	}
	// Independently computed squared lengths: (1,2)=14318, (0,2)=38984, (0,1)=60522.
	TestEqual(TEXT("first tree endpoint"), Out->GetPlan().Connections[0].Start.Id, uint64(1));
	TestEqual(TEXT("first tree second endpoint"), Out->GetPlan().Connections[0].End.Id, uint64(2));
	TestEqual(TEXT("second tree endpoint"), Out->GetPlan().Connections[1].Start.Id, uint64(0));
	TestEqual(TEXT("second tree second endpoint"), Out->GetPlan().Connections[1].End.Id, uint64(2));
	TestEqual(TEXT("segments correspond to links"), Out->GetPlan().Segments.Num(), 2);
	TestEqual(TEXT("entrances remain pending surface phase"), Out->GetPlan().Entrances.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5CandidateRegionTest, "VoxelLand.M5.Candidates.RegionalPortal", M5CandidateFlags)
bool FVoxelLandM5CandidateRegionTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings S = M5CandidateSettings();
	S.PlanningMode = EVoxelLandM5PlanningMode::Regional;
	S.Bounds.MinInclusive.X = -200;
	S.Bounds.MaxExclusive.X = 200;
	S.RegionSizeCm = 200;
	S.RegionOriginXCm = 0;
	S.RegionOriginYCm = -100;
	S.Topology.MinChambers = 1;
	S.Topology.MaxChambers = 1;
	S.Topology.MaxTunnelRadiusCm = 2;
	S.Limits.MaxCandidates = 1;
	FVoxelLandM5RegionCoord Left;
	Left.X = -1;
	FVoxelLandM5RegionCoord Right;
	FM5CandidateHandle A, B;
	if (!TestTrue(TEXT("negative region succeeds"), M5CandidateSuccess(M5CandidateBuild(
														S, 42, Left, [] { return false; }, A)))
		|| !TestTrue(TEXT("positive neighbor succeeds"),
			M5CandidateSuccess(M5CandidateBuild(S, 42, Right, [] { return false; }, B))))
	{
		return false;
	}
	if (!TestEqual(TEXT("one left portal"), A->GetPlan().Portals.Num(), 1)
		|| !TestEqual(TEXT("one right portal"), B->GetPlan().Portals.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("shared ID"), A->GetPlan().Portals[0].Id, B->GetPlan().Portals[0].Id);
	TestEqual(TEXT("face X"), A->GetPlan().Portals[0].CenterCm.X, int64(0));
	TestEqual(TEXT("shared face X"), A->GetPlan().Portals[0].CenterCm.X, B->GetPlan().Portals[0].CenterCm.X);
	TestEqual(TEXT("shared face Y"), A->GetPlan().Portals[0].CenterCm.Y, B->GetPlan().Portals[0].CenterCm.Y);
	TestEqual(TEXT("shared face Z"), A->GetPlan().Portals[0].CenterCm.Z, B->GetPlan().Portals[0].CenterCm.Z);
	TestEqual(TEXT("canonical left X"), A->GetPlan().Portals[0].Key.RegionA.X, int64(-1));
	TestEqual(TEXT("canonical right X"), A->GetPlan().Portals[0].Key.RegionB.X, int64(0));
	TestEqual(TEXT("left portal connected"), A->GetPlan().Connections.Num(), 1);
	TestEqual(TEXT("right portal connected"), B->GetPlan().Connections.Num(), 1);
	if (!TestEqual(TEXT("one left segment"), A->GetPlan().Segments.Num(), 1)
		|| !TestEqual(TEXT("one right segment"), B->GetPlan().Segments.Num(), 1))
	{
		return false;
	}
	const FVoxelLandM5Segment& LeftLink = A->GetPlan().Segments[0];
	const FVoxelLandM5Segment& RightLink = B->GetPlan().Segments[0];
	TestTrue(TEXT("left chamber lies on left side"), LeftLink.StartCm.X < 0);
	TestTrue(TEXT("right chamber lies on right side"), RightLink.StartCm.X > 0);
	TestEqual(TEXT("left link reaches boundary center"), LeftLink.EndCm.X, int64(0));
	TestEqual(TEXT("right link reaches boundary center"), RightLink.EndCm.X, int64(0));
	TestEqual(TEXT("shared endpoint Y"), LeftLink.EndCm.Y, RightLink.EndCm.Y);
	TestEqual(TEXT("shared endpoint Z"), LeftLink.EndCm.Z, RightLink.EndCm.Z);
	TestEqual(TEXT("left link covers shared radius"), LeftLink.RadiusCm, A->GetPlan().Portals[0].RadiusCm);
	TestEqual(TEXT("right link covers shared radius"), RightLink.RadiusCm, B->GetPlan().Portals[0].RadiusCm);
	TestEqual(TEXT("configured portal radius is used"), LeftLink.RadiusCm, 2);
	// Independent sphere-endcap calculation: the face point one centimetre
	// above the common center has squared distance 1, strictly below 2^2.
	const int64 FaceOffsetSquared = 1;
	TestTrue(TEXT("left endcap contains shared face sample"),
		FaceOffsetSquared < int64(LeftLink.RadiusCm) * LeftLink.RadiusCm);
	TestTrue(TEXT("right endcap contains shared face sample"),
		FaceOffsetSquared < int64(RightLink.RadiusCm) * RightLink.RadiusCm);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5CandidateTieTest, "VoxelLand.M5.Candidates.EqualDistanceAndLoop", M5CandidateFlags)
bool FVoxelLandM5CandidateTieTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings S = M5CandidateSettings();
	S.Bounds.MinInclusive.X = S.Bounds.MinInclusive.Y = S.Bounds.MinInclusive.Z = -5;
	S.Bounds.MaxExclusive.X = S.Bounds.MaxExclusive.Y = S.Bounds.MaxExclusive.Z = 5;
	S.Topology.MinChamberRadiusCm = S.Topology.MaxChamberRadiusCm = 1;
	S.Topology.MinLoops = S.Topology.MaxLoops = 1;
	FM5CandidateHandle Out;
	if (!TestTrue(TEXT("equal-distance candidate succeeds"),
			M5CandidateSuccess(M5CandidateBuild(S, 11, {}, [] { return false; }, Out))))
	{
		return false;
	}
	if (!TestEqual(TEXT("three selected connections"), Out->GetPlan().Connections.Num(), 3))
	{
		return false;
	}
	// Independent SHA-256 fixture: (1,2,3),(-3,3,-4),(-2,2,0).
	// Distances for (0,2) and (1,2) both equal 18, ordered by stable IDs.
	TestEqual(TEXT("equal tie first start"), Out->GetPlan().Connections[0].Start.Id, uint64(0));
	TestEqual(TEXT("equal tie first end"), Out->GetPlan().Connections[0].End.Id, uint64(2));
	TestEqual(TEXT("equal tie second start"), Out->GetPlan().Connections[1].Start.Id, uint64(1));
	TestEqual(TEXT("equal tie second end"), Out->GetPlan().Connections[1].End.Id, uint64(2));
	TestFalse(TEXT("first edge is tree"), Out->GetPlan().Connections[0].bLoop);
	TestFalse(TEXT("second edge is tree"), Out->GetPlan().Connections[1].bLoop);
	TestTrue(TEXT("third edge is loop"), Out->GetPlan().Connections[2].bLoop);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5CandidateAttemptsTest, "VoxelLand.M5.Candidates.AttemptsAndRadii", M5CandidateFlags)
bool FVoxelLandM5CandidateAttemptsTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings S = M5CandidateSettings();
	S.Bounds.MinInclusive.X = S.Bounds.MinInclusive.Y = S.Bounds.MinInclusive.Z = -3;
	S.Bounds.MaxExclusive.X = S.Bounds.MaxExclusive.Y = S.Bounds.MaxExclusive.Z = 3;
	S.Topology.MinChambers = S.Topology.MaxChambers = 2;
	S.Topology.MinChamberRadiusCm = S.Topology.MaxChamberRadiusCm = 1;
	S.Limits.MaxCandidates = 2;
	S.Limits.MaxAttempts = 1;
	FM5CandidateHandle Out = M5CandidateSentinel();
	if (!TestTrue(TEXT("sentinel created"), Out.IsValid()))
	{
		return false;
	}
	FVoxelLandM5Diagnostic E = M5CandidateBuild(S, 25, {}, [] { return false; }, Out);
	TestTrue(TEXT("one attempt exhausts spacing"), E.Code == EVoxelLandM5ErrorCode::Blocked);
	TestEqual(TEXT("failed attempts preserve previous output"), Out->GetPlan().Chambers[0].Id, uint64(0));
	S.Limits.MaxAttempts = 2;
	E = M5CandidateBuild(S, 25, {}, [] { return false; }, Out);
	if (!TestTrue(TEXT("second attempt succeeds"), M5CandidateSuccess(E))
		|| !TestEqual(TEXT("two candidates accepted"), Out->GetPlan().Chambers.Num(), 2))
	{
		return false;
	}
	// Independently computed SHA packet: candidate 1 first attempt (1,0,-1)
	// is too near candidate 0 (1,-1,0); second attempt is (-2,1,-1).
	TestEqual(TEXT("retry X"), Out->GetPlan().Chambers[1].CenterCm.X, int64(-2));
	TestEqual(TEXT("retry Y"), Out->GetPlan().Chambers[1].CenterCm.Y, int64(1));
	TestEqual(TEXT("retry Z"), Out->GetPlan().Chambers[1].CenterCm.Z, int64(-1));
	S = M5CandidateSettings();
	S.Topology.MaxTunnelRadiusCm = 2;
	E = M5CandidateBuild(S, 42, {}, [] { return false; }, Out);
	if (!TestTrue(TEXT("radius range accepted"), M5CandidateSuccess(E))
		|| !TestEqual(TEXT("two radius samples"), Out->GetPlan().Segments.Num(), 2))
	{
		return false;
	}
	// Independent packet reference using edge typed IDs and draw slot 64.
	TestEqual(TEXT("first edge radius"), Out->GetPlan().Segments[0].RadiusCm, 2);
	TestEqual(TEXT("second edge radius"), Out->GetPlan().Segments[1].RadiusCm, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5CandidateBudgetAndRankingTest,
	"VoxelLand.M5.Candidates.DenseBudgetAndRankingCancellation", M5CandidateFlags)
bool FVoxelLandM5CandidateBudgetAndRankingTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings S = M5CandidateSettings();
	S.Topology.MinChambers = S.Topology.MaxChambers = 8;
	S.Topology.MinLoops = S.Topology.MaxLoops = 21;
	S.Limits.MaxCandidates = 8;
	S.Limits.MaxEdges = 28;
	S.Limits.MaxSegments = 28;
	S.Limits.MaxBytes = 20000;
	FM5CandidateHandle Out = M5CandidateSentinel();
	if (!TestTrue(TEXT("sentinel created"), Out.IsValid()))
	{
		return false;
	}
	FVoxelLandM5Diagnostic E = M5CandidateBuild(S, 42, {}, [] { return false; }, Out);
	TestTrue(TEXT("dense candidate allocation rejected"), E.Code == EVoxelLandM5ErrorCode::BudgetExceeded);
	TestEqual(TEXT("dense budget preserves prior output"), Out->GetPlan().Chambers[0].Id, uint64(0));
	S = M5CandidateSettings();
	int32 Polls = 0;
	// Seed 42's three first-attempt candidates and all three tree checks complete
	// by poll 38; poll 41 is the first non-tree edge's loop-ranking check.
	E = M5CandidateBuild(S, 42, {}, [&Polls] { return ++Polls >= 41; }, Out);
	TestTrue(TEXT("cancel during loop ranking"), E.Code == EVoxelLandM5ErrorCode::Cancelled);
	TestEqual(TEXT("ranking cancellation polled at expected stage"), Polls, 41);
	TestEqual(TEXT("ranking cancellation preserves prior output"), Out->GetPlan().Chambers[0].Id, uint64(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5CandidateSharedBudgetTest, "VoxelLand.M5.Candidates.SharedBudgetAndLease", M5CandidateFlags)
bool FVoxelLandM5CandidateSharedBudgetTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings S = M5CandidateSettings();
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("create budget"), FVoxelLandM5Budget::TryCreate(1000000, S.Limits.MaxValidationWork, Budget)))
	{
		return false;
	}
	FM5CandidateHandle A;
	FVoxelLandM5Diagnostic E =
		VoxelLand::M5::CandidatePlanner::Build(S, 42, {}, Budget.ToSharedRef(), [] { return false; }, A);
	if (!TestTrue(TEXT("first plan succeeds"), M5CandidateSuccess(E)) || !TestTrue(TEXT("first result"), A.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("result retains originating budget"), A->UsesBudget(*Budget));
	int64 Live = 0, Peak = 0, Work = 0;
	Budget->GetUsage(Live, Peak, Work);
	TestTrue(TEXT("result retains reservation"), Live > 0);
	TestTrue(TEXT("planning charges work"), Work > 0);
	const int64 FirstReservation = Live;
	const int64 FirstWork = Work;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> LooseBudget;
	FM5CandidateHandle LooseOut = M5CandidateSentinel();
	const FVoxelLandM5CandidateResult* LooseBefore = LooseOut.Get();
	if (!TestTrue(TEXT("create too-large memory budget"),
			FVoxelLandM5Budget::TryCreate(S.Limits.MaxBytes + 1, S.Limits.MaxValidationWork, LooseBudget)))
	{
		return false;
	}
	E = VoxelLand::M5::CandidatePlanner::Build(S, 42, {}, LooseBudget.ToSharedRef(), [] { return false; }, LooseOut);
	TestTrue(TEXT("loose memory limit rejected"), E.Code == EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("loose memory preserves prior result"), LooseOut.Get() == LooseBefore);
	LooseBudget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("loose memory performs no reservation"), Live, int64(0));
	TestEqual(TEXT("loose memory performs no work charge"), Work, int64(0));
	if (!TestTrue(TEXT("create too-large work budget"),
			FVoxelLandM5Budget::TryCreate(S.Limits.MaxBytes, S.Limits.MaxValidationWork + 1, LooseBudget)))
	{
		return false;
	}
	E = VoxelLand::M5::CandidatePlanner::Build(S, 42, {}, LooseBudget.ToSharedRef(), [] { return false; }, LooseOut);
	TestTrue(TEXT("loose work limit rejected"), E.Code == EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("loose work preserves prior result"), LooseOut.Get() == LooseBefore);
	LooseBudget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("loose work performs no reservation"), Live, int64(0));
	TestEqual(TEXT("loose work performs no work charge"), Work, int64(0));
	FVoxelLandM5RegionCoord Region;
	TestTrue(TEXT("original source accepted"), A->MatchesSource(S, 42, Region));
	TestFalse(TEXT("seed change rejected"), A->MatchesSource(S, 43, Region));
	Region.X = -1;
	TestFalse(TEXT("region change rejected"), A->MatchesSource(S, 42, Region));
	Region.X = 0;
	FVoxelLandM5CaveSettings Changed = S;
	Changed.StageId = FGuid(4, 3, 2, 1);
	TestFalse(TEXT("stage change rejected"), A->MatchesSource(Changed, 42, Region));
	Changed = S;
	++Changed.Salt;
	TestFalse(TEXT("salt change rejected"), A->MatchesSource(Changed, 42, Region));
	Changed = S;
	++Changed.Topology.MaxTunnelRadiusCm;
	TestFalse(TEXT("topology change rejected"), A->MatchesSource(Changed, 42, Region));
	Changed = S;
	++Changed.Pockets.ThresholdQ16;
	TestFalse(TEXT("pocket change rejected"), A->MatchesSource(Changed, 42, Region));
	Changed = S;
	++Changed.Limits.MaxBytes;
	TestFalse(TEXT("resource policy change rejected"), A->MatchesSource(Changed, 42, Region));
	Changed = S;
	++Changed.Limits.MaxJunctions;
	TestFalse(TEXT("junction budget change rejected"), A->MatchesSource(Changed, 42, Region));
	Changed = S;
	++Changed.Limits.MaxRoutes;
	TestFalse(TEXT("route budget change rejected"), A->MatchesSource(Changed, 42, Region));
	Changed = S;
	++Changed.Limits.MaxRouteLegs;
	TestFalse(TEXT("route-leg budget change rejected"), A->MatchesSource(Changed, 42, Region));
	Changed = S;
	++Changed.Limits.MaxRouteFeatureRefs;
	TestFalse(TEXT("route-feature budget change rejected"), A->MatchesSource(Changed, 42, Region));
	FM5CandidateHandle Shared = A;
	A.Reset();
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("shared reader retains reservation"), Live, FirstReservation);
	Shared.Reset();
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("last reader releases reservation"), Live, int64(0));
	TestEqual(TEXT("work remains cumulative"), Work, FirstWork);

	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> LimitedBytes;
	if (!TestTrue(TEXT("create shared-byte budget"),
			FVoxelLandM5Budget::TryCreate(FirstReservation, S.Limits.MaxValidationWork, LimitedBytes)))
	{
		return false;
	}
	FM5CandidateHandle Retained, Second;
	E = VoxelLand::M5::CandidatePlanner::Build(S, 42, {}, LimitedBytes.ToSharedRef(), [] { return false; }, Retained);
	if (!TestTrue(TEXT("first fills reservation"), M5CandidateSuccess(E)))
	{
		return false;
	}
	E = VoxelLand::M5::CandidatePlanner::Build(S, 42, {}, LimitedBytes.ToSharedRef(), [] { return false; }, Second);
	TestTrue(TEXT("concurrent output exceeds shared bytes"), E.Code == EVoxelLandM5ErrorCode::BudgetExceeded);
	TestFalse(TEXT("failed build publishes nothing"), Second.IsValid());
	Retained.Reset();
	E = VoxelLand::M5::CandidatePlanner::Build(S, 42, {}, LimitedBytes.ToSharedRef(), [] { return false; }, Second);
	TestTrue(TEXT("freed reservation permits retry"), M5CandidateSuccess(E));

	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> LimitedWork;
	if (!TestTrue(TEXT("create shared-work budget"), FVoxelLandM5Budget::TryCreate(1000000, FirstWork, LimitedWork)))
	{
		return false;
	}
	FM5CandidateHandle First, Unchanged = M5CandidateSentinel();
	const FVoxelLandM5CandidateResult* Before = Unchanged.Get();
	E = VoxelLand::M5::CandidatePlanner::Build(S, 42, {}, LimitedWork.ToSharedRef(), [] { return false; }, First);
	if (!TestTrue(TEXT("first consumes work allowance"), M5CandidateSuccess(E)))
	{
		return false;
	}
	First.Reset();
	E = VoxelLand::M5::CandidatePlanner::Build(S, 42, {}, LimitedWork.ToSharedRef(), [] { return false; }, Unchanged);
	TestTrue(TEXT("work accumulates across released plans"), E.Code == EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("failure preserves previous result"), Unchanged.Get() == Before);
	LimitedWork->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("failure releases temporary reservation"), Live, int64(0));
	TestEqual(TEXT("failed admission does not charge work"), Work, FirstWork);

	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> StageBudget;
	if (!TestTrue(TEXT("create per-stage budget"), FVoxelLandM5Budget::TryCreate(1000000, FirstWork - 1, StageBudget)))
	{
		return false;
	}
	S.Limits.MaxValidationWork = FirstWork - 1;
	E = VoxelLand::M5::CandidatePlanner::Build(S, 42, {}, StageBudget.ToSharedRef(), [] { return false; }, Unchanged);
	TestTrue(TEXT("stage work below required amount rejected"), E.Code == EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("stage work failure preserves result"), Unchanged.Get() == Before);
	StageBudget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("stage limit failure releases bytes"), Live, int64(0));
	TestEqual(TEXT("stage limit failure does not charge work"), Work, int64(0));
	S.Limits.MaxValidationWork = FirstWork;
	if (!TestTrue(TEXT("create exact stage budget"), FVoxelLandM5Budget::TryCreate(1000000, FirstWork, StageBudget)))
	{
		return false;
	}
	E = VoxelLand::M5::CandidatePlanner::Build(S, 42, {}, StageBudget.ToSharedRef(), [] { return false; }, Unchanged);
	TestTrue(TEXT("stage work exact bound succeeds"), M5CandidateSuccess(E));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5CandidateFailureTest, "VoxelLand.M5.Candidates.FailureAtomicity", M5CandidateFlags)
bool FVoxelLandM5CandidateFailureTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings S = M5CandidateSettings();
	FM5CandidateHandle Out = M5CandidateSentinel();
	if (!TestTrue(TEXT("sentinel created"), Out.IsValid()))
	{
		return false;
	}
	S.Limits.MaxEdges = 1;
	FVoxelLandM5Diagnostic E = M5CandidateBuild(S, 42, {}, [] { return false; }, Out);
	TestTrue(TEXT("budget rejected"), E.Code == EVoxelLandM5ErrorCode::BudgetExceeded);
	TestEqual(TEXT("budget preserves output"), Out->GetPlan().Chambers[0].Id, uint64(0));
	S = M5CandidateSettings();
	E = M5CandidateBuild(S, 42, {}, [] { return true; }, Out);
	TestTrue(TEXT("cancel rejected"), E.Code == EVoxelLandM5ErrorCode::Cancelled);
	TestEqual(TEXT("cancellation preserves output"), Out->GetPlan().Chambers[0].Id, uint64(0));
	int32 Polls = 0;
	E = M5CandidateBuild(S, 42, {}, [&Polls] { return ++Polls >= 8; }, Out);
	TestTrue(TEXT("mid-plan cancellation rejected"), E.Code == EVoxelLandM5ErrorCode::Cancelled);
	TestEqual(TEXT("mid-plan cancellation preserves output"), Out->GetPlan().Chambers[0].Id, uint64(0));
	S.Bounds.MaxExclusive.X = S.Bounds.MinInclusive.X;
	E = M5CandidateBuild(S, 42, {}, [] { return false; }, Out);
	TestTrue(TEXT("empty bounds rejected"), E.Code == EVoxelLandM5ErrorCode::InvalidInput);
	TestEqual(TEXT("invalid input preserves output"), Out->GetPlan().Chambers[0].Id, uint64(0));
	return true;
}
#endif
