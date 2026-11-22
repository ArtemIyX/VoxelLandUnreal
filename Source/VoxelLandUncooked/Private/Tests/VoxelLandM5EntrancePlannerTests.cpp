#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5EntrancePlanner.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CandidatePlanner.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5Topology.h"
#include "Misc/AutomationTest.h"

// Automation-only friend: input arrays are explicitly admitted before this
// fixture creates an immutable scratch result. Production has no import path.
class FVoxelLandM5EntranceFixtureFactory
{
public:
	static TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Create(
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, const FVoxelLandM5CaveSettings& Settings,
		int64 Seed, const FVoxelLandM5CavePlan& Input)
	{
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
		if (!Budget->TryReserveBytes(65536, Lease))
		{
			return nullptr;
		}
		FVoxelLandM5CavePlan Copy = Input;
		return MakeShareable(
			new FVoxelLandM5CandidateResult(Budget, Settings, Seed, Input.Region, MoveTemp(Copy), MoveTemp(Lease)));
	}
};

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
	S.Topology.MinChambers = S.Topology.MaxChambers = 1;
	S.Topology.EntranceCount = 1;
	S.Topology.MinLoops = S.Topology.MaxLoops = 0;
	S.Topology.MinTunnelRadiusCm = S.Topology.MaxTunnelRadiusCm = 100;
	S.Topology.MinChamberRadiusCm = S.Topology.MaxChamberRadiusCm = 200;
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

FVoxelLandM5Diagnostic M5RunEntrance(const FVoxelLandM5CaveSettings& Settings, int64 Seed,
	const FVoxelLandM5CavePlan& Input,
	TFunctionRef<bool(int64, int64, int32, int64&, int64&, int64&, int32&, bool&, bool&)> Query,
	TFunctionRef<bool()> IsCancelled, FVoxelLandM5CavePlan& Out)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!FVoxelLandM5Budget::TryCreate(Settings.Limits.MaxBytes, Settings.Limits.MaxValidationWork, Budget))
	{
		FVoxelLandM5Diagnostic Error;
		Error.Code = EVoxelLandM5ErrorCode::BudgetExceeded;
		return Error;
	}
	const TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate =
		FVoxelLandM5EntranceFixtureFactory::Create(Budget.ToSharedRef(), Settings, Seed, Input);
	if (!Candidate.IsValid())
	{
		FVoxelLandM5Diagnostic Error;
		Error.Code = EVoxelLandM5ErrorCode::BudgetExceeded;
		return Error;
	}
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Result;
	const auto TestQuery = [&Query](TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> SharedBudget, int64 X, int64 Y,
							   int32 Radius, int64& Z, int64& MinZ, int64& MaxZ, int32& Slope, bool& bProtected,
							   bool& bFlooded) {
		FVoxelLandM5Diagnostic Error;
		if (!SharedBudget->TryChargeWork(1))
		{
			Error.Code = EVoxelLandM5ErrorCode::BudgetExceeded;
		}
		else if (!Query(X, Y, Radius, Z, MinZ, MaxZ, Slope, bProtected, bFlooded))
		{
			Error.Code = EVoxelLandM5ErrorCode::Unproven;
		}
		return Error;
	};
	const FVoxelLandM5Diagnostic Error = VoxelLand::M5::EntrancePlanner::Build(
		Settings, Seed, Candidate.ToSharedRef(), Budget.ToSharedRef(), TestQuery, IsCancelled, Result);
	if (Error.Code == EVoxelLandM5ErrorCode::None && Result.IsValid())
	{
		Out = Result->GetPlan(); // Fixture-only copy, not a published plan.
	}
	return Error;
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
		return M5RunEntrance(S, 42, Input, Query, [] { return false; }, Out);
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
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> TopologyBudget;
	if (!TestTrue(TEXT("topology budget"), FVoxelLandM5Budget::TryCreate(1000000, 1000000, TopologyBudget)))
	{
		return false;
	}
	const auto TopologyCandidate = FVoxelLandM5EntranceFixtureFactory::Create(TopologyBudget.ToSharedRef(), S, 42, A);
	if (!TestTrue(TEXT("admit topology fixture"), TopologyCandidate.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("typed link connected"), VoxelLand::M5::Topology::Validate(S, 42, A.Region,
											   TopologyCandidate.ToSharedRef(), TopologyBudget.ToSharedRef(),
											   [] {
												   return false;
											   }).Code
											   == EVoxelLandM5ErrorCode::None);
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
	FVoxelLandM5CavePlan WithRoutes = Input;
	FVoxelLandM5Route Route;
	Route.Legs.AddDefaulted();
	Route.CoveredFeatures.AddDefaulted();
	WithRoutes.Routes.Add(MoveTemp(Route));
	int32 RouteQueryCount = 0;
	const auto RouteQuery = [&RouteQueryCount](int64 X, int64 Y, int32 Radius, int64& Z, int64& MinZ, int64& MaxZ,
								int32& Slope, bool& bProtected, bool& bFlooded) {
		++RouteQueryCount;
		return M5FlatSurface(X, Y, Radius, Z, MinZ, MaxZ, Slope, bProtected, bFlooded);
	};
	TestTrue(TEXT("nested routes rejected before copy"), M5RunEntrance(
															 S, 42, WithRoutes, RouteQuery, [] { return false; }, Out)
																 .Code
															 == EVoxelLandM5ErrorCode::Unproven);
	TestEqual(TEXT("nested routes skip surface query"), RouteQueryCount, 0);
	TestEqual(TEXT("nested routes preserve previous output"), Out.Region.X, int64(733));
	FVoxelLandM5CavePlan WithJunctions = Input;
	WithJunctions.Junctions.AddDefaulted();
	TestTrue(
		TEXT("junction records rejected before copy"), M5RunEntrance(
														   S, 42, WithJunctions, RouteQuery, [] { return false; }, Out)
															   .Code
														   == EVoxelLandM5ErrorCode::Unproven);
	TestEqual(TEXT("junctions skip surface query"), RouteQueryCount, 0);
	TestEqual(TEXT("junctions preserve previous output"), Out.Region.X, int64(733));
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
	TestTrue(TEXT("blocked protection"),
		M5RunEntrance(S, 42, Input, Protected, [] { return false; }, Out).Code == EVoxelLandM5ErrorCode::Blocked);
	TestTrue(TEXT("too-steep footprint"),
		M5RunEntrance(S, 42, Input, Steep, [] { return false; }, Out).Code == EVoxelLandM5ErrorCode::Blocked);
	TestTrue(TEXT("ambiguous final surface"),
		M5RunEntrance(S, 42, Input, Missing, [] { return false; }, Out).Code == EVoxelLandM5ErrorCode::Unproven);
	TestTrue(TEXT("varying footprint is unproven"),
		M5RunEntrance(S, 42, Input, Variable, [] { return false; }, Out).Code == EVoxelLandM5ErrorCode::Unproven);
	TestTrue(TEXT("inconsistent height bounds rejected"), M5RunEntrance(
															  S, 42, Input, Inconsistent, [] { return false; }, Out)
																  .Code
															  == EVoxelLandM5ErrorCode::InvalidInput);
	TestTrue(TEXT("cancelled"),
		M5RunEntrance(S, 42, Input, M5FlatSurface, [] { return true; }, Out).Code == EVoxelLandM5ErrorCode::Cancelled);
	FVoxelLandM5CavePlan Deep = Input;
	Deep.Chambers[0].CenterCm.Z = -950;
	TestTrue(TEXT("deep approach is unproven"),
		M5RunEntrance(S, 42, Deep, M5FlatSurface, [] { return false; }, Out).Code == EVoxelLandM5ErrorCode::Unproven);
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
		M5RunEntrance(S, 42, SearchInput, SurfaceAndArm, CancelDuringSearch, Out).Code
			== EVoxelLandM5ErrorCode::Cancelled);
	TestTrue(TEXT("ID search cancellation reached"), SearchChecks >= 5);
	S.Water.Policy = EVoxelLandM5WaterPolicy::RejectFlooded;
	TestTrue(TEXT("flooding rejected"),
		M5RunEntrance(S, 42, Input, Wet, [] { return false; }, Out).Code == EVoxelLandM5ErrorCode::Blocked);
	S.Water.Policy = EVoxelLandM5WaterPolicy::AllowAndMark;
	FVoxelLandM5CavePlan WetOut;
	if (TestTrue(TEXT("flooding allowed"), M5RunEntrance(
											   S, 42, Input, Wet, [] { return false; }, WetOut)
												   .Code
											   == EVoxelLandM5ErrorCode::None)
		&& TestEqual(TEXT("flooded entrance count"), WetOut.Entrances.Num(), 1))
	{
		TestTrue(TEXT("flooding is recorded"), WetOut.Entrances[0].bFlooded);
	}
	S.Limits.MaxBytes = 1;
	TestTrue(TEXT("budget rejected"), M5RunEntrance(
										  S, 42, Input, M5FlatSurface, [] { return false; }, Out)
											  .Code
										  == EVoxelLandM5ErrorCode::BudgetExceeded);
	TestEqual(TEXT("failure preserves existing output"), Out.Region.X, int64(733));
	TestEqual(TEXT("failure publishes no entrance"), Out.Entrances.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5EntranceBudgetTest, "VoxelLand.M5.Entrance.SharedBudget", M5EntranceFlags)
bool FVoxelLandM5EntranceBudgetTest::RunTest(const FString& Parameters)
{
	const FVoxelLandM5CaveSettings S = M5EntranceSettings();
	const FVoxelLandM5CavePlan Input = M5EntranceCandidate(S);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, Other;
	if (!TestTrue(TEXT("create budget"), FVoxelLandM5Budget::TryCreate(1000000, 1000000, Budget))
		|| !TestTrue(TEXT("create other budget"), FVoxelLandM5Budget::TryCreate(1000000, 1000000, Other)))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate =
		FVoxelLandM5EntranceFixtureFactory::Create(Budget.ToSharedRef(), S, 42, Input);
	if (!TestTrue(TEXT("admit candidate"), Candidate.IsValid()))
	{
		return false;
	}
	int32 QueryCount = 0;
	const auto Query = [&QueryCount](TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Shared, int64 X, int64 Y,
						   int32 Radius, int64& Z, int64& MinZ, int64& MaxZ, int32& Slope, bool& Protected,
						   bool& Flooded) {
		++QueryCount;
		FVoxelLandM5Diagnostic Error;
		if (!Shared->TryChargeWork(1))
		{
			Error.Code = EVoxelLandM5ErrorCode::BudgetExceeded;
		}
		else
		{
			M5FlatSurface(X, Y, Radius, Z, MinZ, MaxZ, Slope, Protected, Flooded);
		}
		return Error;
	};
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Out = Candidate;
	TestTrue(TEXT("foreign budget rejected"),
		VoxelLand::M5::EntrancePlanner::Build(
			S, 42, Candidate.ToSharedRef(), Other.ToSharedRef(), Query, [] { return false; }, Out)
				.Code
			== EVoxelLandM5ErrorCode::InvalidInput);
	TestEqual(TEXT("foreign budget never reaches query"), QueryCount, 0);
	TestTrue(TEXT("failed call preserves output"), Out == Candidate);
	FVoxelLandM5CaveSettings Changed = S;
	Changed.Topology.MaxTunnelRadiusCm += 1;
	TestTrue(TEXT("source settings mismatch rejected"),
		VoxelLand::M5::EntrancePlanner::Build(
			Changed, 42, Candidate.ToSharedRef(), Budget.ToSharedRef(), Query, [] { return false; }, Out)
				.Code
			== EVoxelLandM5ErrorCode::InvalidInput);
	TestTrue(TEXT("source seed mismatch rejected"),
		VoxelLand::M5::EntrancePlanner::Build(
			S, 43, Candidate.ToSharedRef(), Budget.ToSharedRef(), Query, [] { return false; }, Out)
				.Code
			== EVoxelLandM5ErrorCode::InvalidInput);
	TestEqual(TEXT("mismatched source never reaches query"), QueryCount, 0);
	TestTrue(TEXT("mismatched source preserves output"), Out == Candidate);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> NoWork;
	if (!TestTrue(TEXT("create exhausted-work budget"), FVoxelLandM5Budget::TryCreate(1000000, 0, NoWork)))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> NoWorkCandidate =
		FVoxelLandM5EntranceFixtureFactory::Create(NoWork.ToSharedRef(), S, 42, Input);
	if (!TestTrue(TEXT("admit no-work candidate"), NoWorkCandidate.IsValid()))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> NoWorkOut = NoWorkCandidate;
	TestTrue(TEXT("work budget rejected"),
		VoxelLand::M5::EntrancePlanner::Build(
			S, 42, NoWorkCandidate.ToSharedRef(), NoWork.ToSharedRef(), Query, [] { return false; }, NoWorkOut)
				.Code
			== EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("work failure preserves result"), NoWorkOut == NoWorkCandidate);
	TestEqual(TEXT("work failure never reaches query"), QueryCount, 0);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Loose;
	if (!TestTrue(TEXT("create overbroad budget"),
			FVoxelLandM5Budget::TryCreate(S.Limits.MaxBytes + 1, S.Limits.MaxValidationWork, Loose)))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> LooseCandidate =
		FVoxelLandM5EntranceFixtureFactory::Create(Loose.ToSharedRef(), S, 42, Input);
	if (!TestTrue(TEXT("admit loose candidate"), LooseCandidate.IsValid()))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> LooseOut = LooseCandidate;
	TestTrue(TEXT("loose budget rejected before query"),
		VoxelLand::M5::EntrancePlanner::Build(
			S, 42, LooseCandidate.ToSharedRef(), Loose.ToSharedRef(), Query, [] { return false; }, LooseOut)
				.Code
			== EVoxelLandM5ErrorCode::InvalidInput);
	TestEqual(TEXT("loose budget skips query"), QueryCount, 0);
	TestTrue(TEXT("loose budget preserves output"), LooseOut == LooseCandidate);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> LooseWork;
	if (!TestTrue(TEXT("create overbroad work budget"),
			FVoxelLandM5Budget::TryCreate(S.Limits.MaxBytes, S.Limits.MaxValidationWork + 1, LooseWork)))
	{
		return false;
	}
	const auto LooseWorkCandidate = FVoxelLandM5EntranceFixtureFactory::Create(LooseWork.ToSharedRef(), S, 42, Input);
	if (!TestTrue(TEXT("admit loose-work candidate"), LooseWorkCandidate.IsValid()))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> LooseWorkOut = LooseWorkCandidate;
	TestTrue(TEXT("loose work limit rejected before query"),
		VoxelLand::M5::EntrancePlanner::Build(
			S, 42, LooseWorkCandidate.ToSharedRef(), LooseWork.ToSharedRef(), Query, [] { return false; }, LooseWorkOut)
				.Code
			== EVoxelLandM5ErrorCode::InvalidInput);
	TestEqual(TEXT("loose work budget skips query"), QueryCount, 0);
	TestTrue(TEXT("loose work budget preserves output"), LooseWorkOut == LooseWorkCandidate);
	// Independent admission reference for this fixture: 1024 fixed + 8
	// duplicate-scan units + 8 connection and 64 segment ID-search units +
	// four attempts * (2*(2*64+1) + 16 chamber + 32 overhead) = 2328.
	// The query charges one additional unit, so 2329 succeeds and 2327 fails
	// at entrance admission before the query is invoked.
	for (int64 MaxWork : {int64(2327), int64(2329)})
	{
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Bounded;
		if (!TestTrue(TEXT("create boundary budget"), FVoxelLandM5Budget::TryCreate(1000000, MaxWork, Bounded)))
		{
			return false;
		}
		const auto BoundedCandidate = FVoxelLandM5EntranceFixtureFactory::Create(Bounded.ToSharedRef(), S, 42, Input);
		if (!TestTrue(TEXT("admit boundary candidate"), BoundedCandidate.IsValid()))
		{
			return false;
		}
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> BoundaryOut;
		int32 CallsBefore = QueryCount;
		const FVoxelLandM5Diagnostic Error = VoxelLand::M5::EntrancePlanner::Build(
			S, 42, BoundedCandidate.ToSharedRef(), Bounded.ToSharedRef(), Query, [] { return false; }, BoundaryOut);
		TestTrue(TEXT("exact work boundary"),
			Error.Code == (MaxWork == 2329 ? EVoxelLandM5ErrorCode::None : EVoxelLandM5ErrorCode::BudgetExceeded));
		TestEqual(TEXT("query charged only after admission"), QueryCount - CallsBefore, MaxWork == 2329 ? 1 : 0);
	}
	if (!TestTrue(TEXT("same budget accepted"),
			VoxelLand::M5::EntrancePlanner::Build(
				S, 42, Candidate.ToSharedRef(), Budget.ToSharedRef(), Query, [] { return false; }, Out)
					.Code
				== EVoxelLandM5ErrorCode::None))
	{
		return false;
	}
	int64 Live = 0, Peak = 0, Work = 0;
	Budget->GetUsage(Live, Peak, Work);
	TestTrue(TEXT("candidate and result charged"), Live > 65536);
	TestTrue(TEXT("work charged cumulatively"), Work > 0);
	Candidate.Reset();
	Budget->GetUsage(Live, Peak, Work);
	TestTrue(TEXT("result retains admission"), Live > 0);
	Out.Reset();
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("last result releases admission"), Live, int64(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5CandidateEntranceCumulativeTest, "VoxelLand.M5.Entrance.CandidateCumulativeBudget", M5EntranceFlags)
bool FVoxelLandM5CandidateEntranceCumulativeTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings S = M5EntranceSettings();
	S.Topology.MinChambers = S.Topology.MaxChambers = 1;
	S.Topology.MinChamberRadiusCm = S.Topology.MaxChamberRadiusCm = 200;
	S.Topology.MinTunnelRadiusCm = S.Topology.MaxTunnelRadiusCm = 100;
	S.Topology.MinLoops = S.Topology.MaxLoops = 0;
	S.Limits.MaxCandidates = 1;
	// Independent work estimate: 1024 fixed + 4*(4*129+1+8)
	// candidate attempts + 2*129 random-selection + 8 node work = 3390.
	S.Limits.MaxValidationWork = 3390;
	const FVoxelLandM5CavePlan EntranceFixture = M5EntranceCandidate(S);
	FVoxelLandM5CavePlan EntranceOnly;
	if (!TestTrue(TEXT("entrance alone fits same cap"),
			M5RunEntrance(
				S, 42, EntranceFixture, M5FlatSurface, [] { return false; }, EntranceOnly)
					.Code
				== EVoxelLandM5ErrorCode::None))
	{
		return false;
	}
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("create shared bounded budget"),
			FVoxelLandM5Budget::TryCreate(S.Limits.MaxBytes, S.Limits.MaxValidationWork, Budget)))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate;
	if (!TestTrue(
			TEXT("candidate alone fits same cap"), VoxelLand::M5::CandidatePlanner::Build(
													   S, 42, {}, Budget.ToSharedRef(), [] { return false; }, Candidate)
														   .Code
													   == EVoxelLandM5ErrorCode::None)
		|| !TestTrue(TEXT("candidate retained"), Candidate.IsValid()))
	{
		return false;
	}
	int64 Live = 0, Peak = 0, Work = 0;
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("candidate charged independently calculated work"), Work, int64(3390));
	int32 QueryCalls = 0;
	const auto Query = [&QueryCalls](TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Shared, int64 X, int64 Y,
						   int32 Radius, int64& Z, int64& MinZ, int64& MaxZ, int32& Slope, bool& Protected,
						   bool& Flooded) {
		++QueryCalls;
		FVoxelLandM5Diagnostic Error;
		if (!Shared->TryChargeWork(1))
		{
			Error.Code = EVoxelLandM5ErrorCode::BudgetExceeded;
		}
		else
		{
			M5FlatSurface(X, Y, Radius, Z, MinZ, MaxZ, Slope, Protected, Flooded);
		}
		return Error;
	};
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Out = Candidate;
	TestTrue(TEXT("cumulative work blocks second stage"),
		VoxelLand::M5::EntrancePlanner::Build(
			S, 42, Candidate.ToSharedRef(), Budget.ToSharedRef(), Query, [] { return false; }, Out)
				.Code
			== EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("rejected entrance preserves candidate output"), Out == Candidate);
	TestEqual(TEXT("cumulative rejection occurs before query"), QueryCalls, 0);
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("failed work admission does not increment charge"), Work, int64(3390));
	return true;
}
#endif
