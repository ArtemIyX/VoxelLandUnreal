#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5CandidatePlanner.h"
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5CandidateFiniteTest, "VoxelLand.M5.Candidates.FiniteReference", M5CandidateFlags)
bool FVoxelLandM5CandidateFiniteTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings S = M5CandidateSettings();
	FVoxelLandM5CavePlan Out;
	const FVoxelLandM5Diagnostic Error = VoxelLand::M5::CandidatePlanner::Build(S, 42, {}, [] { return false; }, Out);
	if (!TestTrue(TEXT("finite candidate succeeds"), M5CandidateSuccess(Error)))
	{
		return false;
	}
	if (!TestEqual(TEXT("three accepted chambers"), Out.Chambers.Num(), 3)
		|| !TestEqual(TEXT("two spanning tree edges"), Out.Connections.Num(), 2))
	{
		return false;
	}
	// Independently calculated from Python hashlib SHA-256 and the documented packet.
	const int64 Expected[3][3] = {{23, -82, 95}, {-29, 91, -72}, {53, 96, 15}};
	for (int32 I = 0; I < 3; ++I)
	{
		TestEqual(TEXT("candidate X"), Out.Chambers[I].CenterCm.X, Expected[I][0]);
		TestEqual(TEXT("candidate Y"), Out.Chambers[I].CenterCm.Y, Expected[I][1]);
		TestEqual(TEXT("candidate Z"), Out.Chambers[I].CenterCm.Z, Expected[I][2]);
		TestEqual(TEXT("stable candidate ID"), Out.Chambers[I].Id, uint64(I));
	}
	// Independently computed squared lengths: (1,2)=14318, (0,2)=38984, (0,1)=60522.
	TestEqual(TEXT("first tree endpoint"), Out.Connections[0].Start.Id, uint64(1));
	TestEqual(TEXT("first tree second endpoint"), Out.Connections[0].End.Id, uint64(2));
	TestEqual(TEXT("second tree endpoint"), Out.Connections[1].Start.Id, uint64(0));
	TestEqual(TEXT("second tree second endpoint"), Out.Connections[1].End.Id, uint64(2));
	TestEqual(TEXT("segments correspond to links"), Out.Segments.Num(), 2);
	TestEqual(TEXT("entrances remain pending surface phase"), Out.Entrances.Num(), 0);
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
	FVoxelLandM5CavePlan A, B;
	if (!TestTrue(TEXT("negative region succeeds"), M5CandidateSuccess(VoxelLand::M5::CandidatePlanner::Build(
														S, 42, Left, [] { return false; }, A)))
		|| !TestTrue(TEXT("positive neighbor succeeds"),
			M5CandidateSuccess(VoxelLand::M5::CandidatePlanner::Build(S, 42, Right, [] { return false; }, B))))
	{
		return false;
	}
	if (!TestEqual(TEXT("one left portal"), A.Portals.Num(), 1)
		|| !TestEqual(TEXT("one right portal"), B.Portals.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("shared ID"), A.Portals[0].Id, B.Portals[0].Id);
	TestEqual(TEXT("face X"), A.Portals[0].CenterCm.X, int64(0));
	TestEqual(TEXT("shared face X"), A.Portals[0].CenterCm.X, B.Portals[0].CenterCm.X);
	TestEqual(TEXT("shared face Y"), A.Portals[0].CenterCm.Y, B.Portals[0].CenterCm.Y);
	TestEqual(TEXT("shared face Z"), A.Portals[0].CenterCm.Z, B.Portals[0].CenterCm.Z);
	TestEqual(TEXT("canonical left X"), A.Portals[0].Key.RegionA.X, int64(-1));
	TestEqual(TEXT("canonical right X"), A.Portals[0].Key.RegionB.X, int64(0));
	TestEqual(TEXT("left portal connected"), A.Connections.Num(), 1);
	TestEqual(TEXT("right portal connected"), B.Connections.Num(), 1);
	if (!TestEqual(TEXT("one left segment"), A.Segments.Num(), 1)
		|| !TestEqual(TEXT("one right segment"), B.Segments.Num(), 1))
	{
		return false;
	}
	const FVoxelLandM5Segment& LeftLink = A.Segments[0];
	const FVoxelLandM5Segment& RightLink = B.Segments[0];
	TestTrue(TEXT("left chamber lies on left side"), LeftLink.StartCm.X < 0);
	TestTrue(TEXT("right chamber lies on right side"), RightLink.StartCm.X > 0);
	TestEqual(TEXT("left link reaches boundary center"), LeftLink.EndCm.X, int64(0));
	TestEqual(TEXT("right link reaches boundary center"), RightLink.EndCm.X, int64(0));
	TestEqual(TEXT("shared endpoint Y"), LeftLink.EndCm.Y, RightLink.EndCm.Y);
	TestEqual(TEXT("shared endpoint Z"), LeftLink.EndCm.Z, RightLink.EndCm.Z);
	TestEqual(TEXT("left link covers shared radius"), LeftLink.RadiusCm, A.Portals[0].RadiusCm);
	TestEqual(TEXT("right link covers shared radius"), RightLink.RadiusCm, B.Portals[0].RadiusCm);
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
	FVoxelLandM5CavePlan Out;
	if (!TestTrue(TEXT("equal-distance candidate succeeds"),
			M5CandidateSuccess(VoxelLand::M5::CandidatePlanner::Build(S, 11, {}, [] { return false; }, Out))))
	{
		return false;
	}
	if (!TestEqual(TEXT("three selected connections"), Out.Connections.Num(), 3))
	{
		return false;
	}
	// Independent SHA-256 fixture: (1,2,3),(-3,3,-4),(-2,2,0).
	// Distances for (0,2) and (1,2) both equal 18, ordered by stable IDs.
	TestEqual(TEXT("equal tie first start"), Out.Connections[0].Start.Id, uint64(0));
	TestEqual(TEXT("equal tie first end"), Out.Connections[0].End.Id, uint64(2));
	TestEqual(TEXT("equal tie second start"), Out.Connections[1].Start.Id, uint64(1));
	TestEqual(TEXT("equal tie second end"), Out.Connections[1].End.Id, uint64(2));
	TestFalse(TEXT("first edge is tree"), Out.Connections[0].bLoop);
	TestFalse(TEXT("second edge is tree"), Out.Connections[1].bLoop);
	TestTrue(TEXT("third edge is loop"), Out.Connections[2].bLoop);
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
	FVoxelLandM5CavePlan Out;
	FVoxelLandM5Chamber Original;
	Original.bHasId = true;
	Original.Id = 999;
	Out.Chambers.Add(Original);
	FVoxelLandM5Diagnostic E = VoxelLand::M5::CandidatePlanner::Build(S, 25, {}, [] { return false; }, Out);
	TestTrue(TEXT("one attempt exhausts spacing"), E.Code == EVoxelLandM5ErrorCode::Blocked);
	TestEqual(TEXT("failed attempts preserve previous output"), Out.Chambers[0].Id, uint64(999));
	S.Limits.MaxAttempts = 2;
	E = VoxelLand::M5::CandidatePlanner::Build(S, 25, {}, [] { return false; }, Out);
	if (!TestTrue(TEXT("second attempt succeeds"), M5CandidateSuccess(E))
		|| !TestEqual(TEXT("two candidates accepted"), Out.Chambers.Num(), 2))
	{
		return false;
	}
	// Independently computed SHA packet: candidate 1 first attempt (1,0,-1)
	// is too near candidate 0 (1,-1,0); second attempt is (-2,1,-1).
	TestEqual(TEXT("retry X"), Out.Chambers[1].CenterCm.X, int64(-2));
	TestEqual(TEXT("retry Y"), Out.Chambers[1].CenterCm.Y, int64(1));
	TestEqual(TEXT("retry Z"), Out.Chambers[1].CenterCm.Z, int64(-1));
	S = M5CandidateSettings();
	S.Topology.MaxTunnelRadiusCm = 2;
	E = VoxelLand::M5::CandidatePlanner::Build(S, 42, {}, [] { return false; }, Out);
	if (!TestTrue(TEXT("radius range accepted"), M5CandidateSuccess(E))
		|| !TestEqual(TEXT("two radius samples"), Out.Segments.Num(), 2))
	{
		return false;
	}
	// Independent packet reference using edge typed IDs and draw slot 64.
	TestEqual(TEXT("first edge radius"), Out.Segments[0].RadiusCm, 2);
	TestEqual(TEXT("second edge radius"), Out.Segments[1].RadiusCm, 2);
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
	FVoxelLandM5CavePlan Out;
	FVoxelLandM5Chamber Original;
	Original.bHasId = true;
	Original.Id = 999;
	Out.Chambers.Add(Original);
	FVoxelLandM5Diagnostic E = VoxelLand::M5::CandidatePlanner::Build(S, 42, {}, [] { return false; }, Out);
	TestTrue(TEXT("dense candidate allocation rejected"), E.Code == EVoxelLandM5ErrorCode::BudgetExceeded);
	TestEqual(TEXT("dense budget preserves prior output"), Out.Chambers[0].Id, uint64(999));
	S = M5CandidateSettings();
	int32 Polls = 0;
	// Seed 42's three first-attempt candidates and all three tree checks complete
	// by poll 38; poll 41 is the first non-tree edge's loop-ranking check.
	E = VoxelLand::M5::CandidatePlanner::Build(S, 42, {}, [&Polls] { return ++Polls >= 41; }, Out);
	TestTrue(TEXT("cancel during loop ranking"), E.Code == EVoxelLandM5ErrorCode::Cancelled);
	TestEqual(TEXT("ranking cancellation polled at expected stage"), Polls, 41);
	TestEqual(TEXT("ranking cancellation preserves prior output"), Out.Chambers[0].Id, uint64(999));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5CandidateFailureTest, "VoxelLand.M5.Candidates.FailureAtomicity", M5CandidateFlags)
bool FVoxelLandM5CandidateFailureTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings S = M5CandidateSettings();
	FVoxelLandM5CavePlan Out;
	FVoxelLandM5Chamber Original;
	Original.bHasId = true;
	Original.Id = 999;
	Out.Chambers.Add(Original);
	S.Limits.MaxEdges = 1;
	FVoxelLandM5Diagnostic E = VoxelLand::M5::CandidatePlanner::Build(S, 42, {}, [] { return false; }, Out);
	TestTrue(TEXT("budget rejected"), E.Code == EVoxelLandM5ErrorCode::BudgetExceeded);
	TestEqual(TEXT("budget preserves output"), Out.Chambers[0].Id, uint64(999));
	S = M5CandidateSettings();
	E = VoxelLand::M5::CandidatePlanner::Build(S, 42, {}, [] { return true; }, Out);
	TestTrue(TEXT("cancel rejected"), E.Code == EVoxelLandM5ErrorCode::Cancelled);
	TestEqual(TEXT("cancellation preserves output"), Out.Chambers[0].Id, uint64(999));
	int32 Polls = 0;
	E = VoxelLand::M5::CandidatePlanner::Build(S, 42, {}, [&Polls] { return ++Polls >= 8; }, Out);
	TestTrue(TEXT("mid-plan cancellation rejected"), E.Code == EVoxelLandM5ErrorCode::Cancelled);
	TestEqual(TEXT("mid-plan cancellation preserves output"), Out.Chambers[0].Id, uint64(999));
	S.Bounds.MaxExclusive.X = S.Bounds.MinInclusive.X;
	E = VoxelLand::M5::CandidatePlanner::Build(S, 42, {}, [] { return false; }, Out);
	TestTrue(TEXT("empty bounds rejected"), E.Code == EVoxelLandM5ErrorCode::InvalidInput);
	TestEqual(TEXT("invalid input preserves output"), Out.Chambers[0].Id, uint64(999));
	return true;
}
#endif
