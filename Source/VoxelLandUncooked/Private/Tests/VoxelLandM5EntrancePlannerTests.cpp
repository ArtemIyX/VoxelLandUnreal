#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5EntrancePlanner.h"
#include "Generation/VoxelLandM5Topology.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5EntranceFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandM5CaveSettings M5EntranceSettings()
{
	FVoxelLandM5CaveSettings S;
	S.bEnabled = true;
	S.StageId = FGuid(1, 2, 3, 4);
	S.Bounds.MinInclusive.X = S.Bounds.MinInclusive.Y = S.Bounds.MinInclusive.Z = -1000;
	S.Bounds.MaxExclusive.X = S.Bounds.MaxExclusive.Y = S.Bounds.MaxExclusive.Z = 1000;
	S.Topology.EntranceCount = 1;
	S.Topology.MaxTunnelRadiusCm = 100;
	S.Capsule.ClearanceMarginCm = 0;
	S.Limits.MaxAttempts = 4;
	return S;
}

FVoxelLandM5CavePlan M5EntranceCandidate(const FVoxelLandM5CaveSettings& S)
{
	FVoxelLandM5CavePlan P;
	P.Bounds = S.Bounds;
	FVoxelLandM5Chamber Chamber;
	Chamber.bHasId = true;
	Chamber.Id = 0;
	Chamber.CenterCm.Z = -190;
	Chamber.RadiusCm = 200;
	P.Chambers.Add(Chamber);
	return P;
}

bool M5FlatSurface(
	int64 X, int64 Y, int32 Radius, int64& Z, int64& MinZ, int64& MaxZ, int32& Slope, bool& Protected, bool& Flooded)
{
	Z = MinZ = MaxZ = 0;
	Slope = 0;
	Protected = false;
	Flooded = false;
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5EntranceFlatTest, "VoxelLand.M5.Entrance.FlatAndDeterministic", M5EntranceFlags)
bool FVoxelLandM5EntranceFlatTest::RunTest(const FString& Parameters)
{
	const FVoxelLandM5CaveSettings S = M5EntranceSettings();
	const FVoxelLandM5CavePlan Input = M5EntranceCandidate(S);
	FVoxelLandM5CavePlan A, B;
	int32 QueriedRadius = 0;
	const auto Query = [&QueriedRadius](int64 X, int64 Y, int32 Radius, int64& Z, int64& MinZ, int64& MaxZ,
						   int32& Slope, bool& bProtected, bool& bFlooded) {
		QueriedRadius = Radius;
		return M5FlatSurface(X, Y, Radius, Z, MinZ, MaxZ, Slope, bProtected, bFlooded);
	};
	const auto Run = [&S, &Input, &Query](FVoxelLandM5CavePlan& Out) {
		return VoxelLand::M5::EntrancePlanner::Build(S, 42, Input, Query, [] { return false; }, Out);
	};
	if (!TestTrue(TEXT("flat surface succeeds"), Run(A).Code == EVoxelLandM5ErrorCode::None)
		|| !TestTrue(TEXT("repeat succeeds"), Run(B).Code == EVoxelLandM5ErrorCode::None)
		|| !TestEqual(TEXT("one entrance"), A.Entrances.Num(), 1)
		|| !TestEqual(TEXT("approach and ramp segments"), A.Segments.Num(), 2)
		|| !TestEqual(TEXT("one connection"), A.Connections.Num(), 1)
		|| !TestEqual(TEXT("repeat entrance"), B.Entrances.Num(), 1)
		|| !TestEqual(TEXT("repeat segments"), B.Segments.Num(), 2))
	{
		return false;
	}
	const FVoxelLandM5Segment& Segment = A.Segments[0];
	TestEqual(TEXT("query covers ramp and radius"), QueriedRadius, 670);
	// Independent Python hashlib/struct calculation of the two namespace-3
	// packets and unbiased range conversion [-330,329], seed 42.
	TestEqual(TEXT("independent entrance X"), Segment.EndCm.X, int64(-195));
	TestEqual(TEXT("independent entrance Y"), Segment.EndCm.Y, int64(167));
	TestEqual(TEXT("approach below surface"), Segment.EndCm.Z, int64(-100));
	TestEqual(TEXT("ramp start matches approach"), A.Segments[1].StartCm.Z, Segment.EndCm.Z);
	TestEqual(TEXT("fixed outside height"), A.Segments[1].EndCm.Z, int64(185));
	TestEqual(TEXT("ramp horizontal run"), A.Segments[1].EndCm.X - A.Segments[1].StartCm.X, int64(570));
	TestEqual(TEXT("crossing Y remains inside queried footprint"), A.Segments[1].StartCm.Y, A.Segments[1].EndCm.Y);
	// Independent arithmetic: rise=285 and run=570, so this ramp is below 45 degrees.
	TestEqual(TEXT("ramp rise"), A.Segments[1].EndCm.Z - A.Segments[1].StartCm.Z, int64(285));
	TestTrue(TEXT("approach also within conservative slope bound"),
		90 * FMath::Abs(Segment.EndCm.Z - Segment.StartCm.Z)
			<= 45
				   * FMath::Max(FMath::Abs(Segment.EndCm.X - Segment.StartCm.X),
					   FMath::Abs(Segment.EndCm.Y - Segment.StartCm.Y)));
	TestTrue(TEXT("fixture has off-axis chamber"),
		Segment.StartCm.X != Segment.EndCm.X || Segment.StartCm.Y != Segment.EndCm.Y);
	TestEqual(TEXT("fixed tunnel radius"), Segment.RadiusCm, 100);
	TestEqual(TEXT("segment starts at chamber"), Segment.StartCm.Z, int64(-190));
	TestEqual(TEXT("opening encloses surface"), A.Entrances[0].OpeningBoundsCm.MinInclusive.Z, int64(-100));
	TestEqual(TEXT("opening encloses outside end"), A.Entrances[0].OpeningBoundsCm.MaxExclusive.Z, int64(286));
	TestEqual(TEXT("repeat X"), Segment.EndCm.X, B.Segments[0].EndCm.X);
	TestEqual(TEXT("repeat Y"), Segment.EndCm.Y, B.Segments[0].EndCm.Y);
	TestEqual(TEXT("linked entrance ID"), A.Connections[0].End.Id, A.Entrances[0].Id);
	TestTrue(TEXT("typed link connected"), VoxelLand::M5::Topology::Validate(A, 42, S.StageId, S.Salt, 8, 8, [] {
		return false;
	}).Code == EVoxelLandM5ErrorCode::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5EntranceFailureTest, "VoxelLand.M5.Entrance.FailureAtomic", M5EntranceFlags)
bool FVoxelLandM5EntranceFailureTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings S = M5EntranceSettings();
	const FVoxelLandM5CavePlan Input = M5EntranceCandidate(S);
	FVoxelLandM5CavePlan Out;
	Out.Region.X = 733;
	const auto Protected = [](int64 X, int64 Y, int32 Radius, int64& Z, int64& MinZ, int64& MaxZ, int32& Slope,
							   bool& bProtected, bool& Flooded) {
		Z = MinZ = MaxZ = 0;
		Slope = 0;
		bProtected = true;
		Flooded = false;
		return true;
	};
	const auto Steep = [](int64 X, int64 Y, int32 Radius, int64& Z, int64& MinZ, int64& MaxZ, int32& Slope,
						   bool& bProtected, bool& Flooded) {
		Z = MinZ = MaxZ = 0;
		Slope = 46;
		bProtected = false;
		Flooded = false;
		return true;
	};
	const auto Missing = [](int64 X, int64 Y, int32 Radius, int64& Z, int64& MinZ, int64& MaxZ, int32& Slope,
							 bool& bProtected, bool& Flooded) {
		return false;
	};
	const auto Wet = [](int64 X, int64 Y, int32 Radius, int64& Z, int64& MinZ, int64& MaxZ, int32& Slope,
						 bool& bProtected, bool& Flooded) {
		Z = MinZ = MaxZ = 0;
		Slope = 0;
		bProtected = false;
		Flooded = true;
		return true;
	};
	const auto Variable = [](int64 X, int64 Y, int32 Radius, int64& Z, int64& MinZ, int64& MaxZ, int32& Slope,
							  bool& bProtected, bool& Flooded) {
		Z = 0;
		MinZ = -1;
		MaxZ = 1;
		Slope = 1;
		bProtected = Flooded = false;
		return true;
	};
	const auto Inconsistent = [](int64 X, int64 Y, int32 Radius, int64& Z, int64& MinZ, int64& MaxZ, int32& Slope,
								  bool& bProtected, bool& Flooded) {
		Z = 0;
		MinZ = 1;
		MaxZ = 2;
		Slope = 0;
		bProtected = Flooded = false;
		return true;
	};
	TestTrue(TEXT("blocked protection"), VoxelLand::M5::EntrancePlanner::Build(
											 S, 42, Input, Protected, [] { return false; }, Out)
												 .Code
											 == EVoxelLandM5ErrorCode::Blocked);
	TestTrue(TEXT("too-steep footprint"), VoxelLand::M5::EntrancePlanner::Build(
											  S, 42, Input, Steep, [] { return false; }, Out)
												  .Code
											  == EVoxelLandM5ErrorCode::Blocked);
	TestTrue(TEXT("ambiguous final surface"), VoxelLand::M5::EntrancePlanner::Build(
												  S, 42, Input, Missing, [] { return false; }, Out)
													  .Code
												  == EVoxelLandM5ErrorCode::Unproven);
	TestTrue(TEXT("varying footprint is unproven"), VoxelLand::M5::EntrancePlanner::Build(
														S, 42, Input, Variable, [] { return false; }, Out)
															.Code
														== EVoxelLandM5ErrorCode::Unproven);
	TestTrue(TEXT("inconsistent height bounds rejected"), VoxelLand::M5::EntrancePlanner::Build(
															  S, 42, Input, Inconsistent, [] { return false; }, Out)
																  .Code
															  == EVoxelLandM5ErrorCode::InvalidInput);
	TestTrue(TEXT("cancelled"), VoxelLand::M5::EntrancePlanner::Build(
									S, 42, Input, M5FlatSurface, [] { return true; }, Out)
										.Code
									== EVoxelLandM5ErrorCode::Cancelled);
	FVoxelLandM5CavePlan Deep = Input;
	Deep.Chambers[0].CenterCm.Z = -950;
	TestTrue(TEXT("deep approach is unproven"), VoxelLand::M5::EntrancePlanner::Build(
													S, 42, Deep, M5FlatSurface, [] { return false; }, Out)
														.Code
													== EVoxelLandM5ErrorCode::Unproven);
	FVoxelLandM5CavePlan SearchInput = Input;
	for (uint64 Id = 0; Id < 20; ++Id)
	{
		FVoxelLandM5Connection Connection;
		Connection.bHasId = true;
		Connection.Id = Id;
		SearchInput.Connections.Add(Connection);
	}
	bool bQueryFinished = false;
	int32 SearchChecks = 0;
	const auto SurfaceAndArm = [&bQueryFinished](int64 X, int64 Y, int32 Radius, int64& Z, int64& MinZ, int64& MaxZ,
								   int32& Slope, bool& bProtected, bool& Flooded) {
		bQueryFinished = true;
		return M5FlatSurface(X, Y, Radius, Z, MinZ, MaxZ, Slope, bProtected, Flooded);
	};
	const auto CancelDuringSearch = [&bQueryFinished, &SearchChecks]() {
		return bQueryFinished && ++SearchChecks >= 5;
	};
	TestTrue(TEXT("cancel inside connection ID search"),
		VoxelLand::M5::EntrancePlanner::Build(S, 42, SearchInput, SurfaceAndArm, CancelDuringSearch, Out).Code
			== EVoxelLandM5ErrorCode::Cancelled);
	TestTrue(TEXT("ID search cancellation reached"), SearchChecks >= 5);
	S.Water.Policy = EVoxelLandM5WaterPolicy::RejectFlooded;
	TestTrue(TEXT("flooding rejected"), VoxelLand::M5::EntrancePlanner::Build(
											S, 42, Input, Wet, [] { return false; }, Out)
												.Code
											== EVoxelLandM5ErrorCode::Blocked);
	S.Water.Policy = EVoxelLandM5WaterPolicy::AllowAndMark;
	FVoxelLandM5CavePlan WetOut;
	if (TestTrue(TEXT("flooding allowed"), VoxelLand::M5::EntrancePlanner::Build(
											   S, 42, Input, Wet, [] { return false; }, WetOut)
												   .Code
											   == EVoxelLandM5ErrorCode::None)
		&& TestEqual(TEXT("flooded entrance count"), WetOut.Entrances.Num(), 1))
	{
		TestTrue(TEXT("flooding is recorded"), WetOut.Entrances[0].bFlooded);
	}
	S.Limits.MaxBytes = 1;
	TestTrue(TEXT("budget rejected"), VoxelLand::M5::EntrancePlanner::Build(
										  S, 42, Input, M5FlatSurface, [] { return false; }, Out)
											  .Code
										  == EVoxelLandM5ErrorCode::BudgetExceeded);
	TestEqual(TEXT("failure preserves existing output"), Out.Region.X, int64(733));
	TestEqual(TEXT("failure publishes no entrance"), Out.Entrances.Num(), 0);
	return true;
}
#endif
