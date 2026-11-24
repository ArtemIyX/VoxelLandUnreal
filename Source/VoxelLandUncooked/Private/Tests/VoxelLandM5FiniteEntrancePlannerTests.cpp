#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Generation/VoxelLandM5BaselineWindow.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5CandidatePlanner.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5FiniteEntrancePlanner.h"
#include "Generation/VoxelLandM5FiniteRoutePlanner.h"
#include "Misc/AutomationTest.h"

// Automation-only controlled candidate; production has no import path.
class FVoxelLandM5FiniteEntranceFixtureFactory
{
public:
	static TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Create(
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, const FVoxelLandM5CaveSettings& Settings)
	{
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
		if (!Budget->TryReserveBytes(65536, Lease))
		{
			return nullptr;
		}
		FVoxelLandM5CavePlan Plan;
		Plan.Bounds = Settings.Bounds;
		FVoxelLandM5Chamber Chamber;
		Chamber.bHasId = true;
		Chamber.Id = 0;
		// The existing analytic fixture uses -190 cm beneath a 0 cm surface.
		// This real M4 window has a 50 cm surface, so shift the chamber by 50 cm.
		Chamber.CenterCm.Z = -140;
		Chamber.RadiusCm = 200;
		Plan.Chambers.Add(Chamber);
		return MakeShareable(new FVoxelLandM5CandidateResult(
			Budget, Settings, 42, Plan.Region, MoveTemp(Plan), MoveTemp(Lease)));
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5FiniteEntrancePlannerTest, "VoxelLand.M5.FiniteEntrancePlanner.Window",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5FiniteEntrancePlannerTest::RunTest(const FString& Parameters)
{
	bool bZeroEntrances = false, bQueryReached = false, bProtected = false;
	bool bWaterRejected = false, bMaskRejected = false, bModeRejected = false;
	bool bForeignBudget = false, bCancelled = false, bNegativeWork = false;
	bool bRealEntrance = false, bInputUnchanged = false, bEntranceRoute = false;
	FString RouteFailure;
	const bool bSetup = Async(EAsyncExecution::ThreadPool, [&]() {
		const auto Never = [] { return false; };
		FVoxelLandProgramDraft Draft;
		Draft.RegisterCount = 1;
		FVoxelLandInstruction Input, Store;
		Input.Op = EVoxelLandOpcode::LoadZ;
		Store.Op = EVoxelLandOpcode::StoreDensity;
		Draft.Instructions = {Input, Store};
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (FVoxelLandProgram::Validate(Draft, Program) || !Program)
		{
			return false;
		}
		TArray<int32> Heights;
		Heights.Init(50 * 256, 81 * 81);
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
		if (!FVoxelLandM4HeightGrid::Create(-4000, -4000, 80, 80, 100, Heights, Grid))
		{
			return false;
		}
		FVoxelLandGeneratorSettings GeneratorSettings;
		GeneratorSettings.Program = Program;
		GeneratorSettings.Profile.VoxelSizeCm = 100.f;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
		if (FVoxelLandGenerator::CreateM4HeightField(GeneratorSettings, Grid, false, Generator) || !Generator)
		{
			return false;
		}
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
		if (!FVoxelLandM5Budget::TryCreate(32 * 1024 * 1024, 100000000, Budget))
		{
			return false;
		}
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Window;
		if (FVoxelLandM5BaselineWindow::Build(Generator.ToSharedRef(), FIntVector(-1, -1, -1),
				FIntVector(1, 1, 1), 8, Budget.ToSharedRef(), Never, Window).Code != EVoxelLandM5ErrorCode::None
			|| !Window)
		{
			return false;
		}
		FVoxelLandM5CaveSettings Settings;
		Settings.bEnabled = true;
		Settings.StageId = FGuid(1, 2, 3, 4);
		Settings.Bounds.MinInclusive.X = Settings.Bounds.MinInclusive.Y = Settings.Bounds.MinInclusive.Z = -1000;
		Settings.Bounds.MaxExclusive.X = Settings.Bounds.MaxExclusive.Y = Settings.Bounds.MaxExclusive.Z = 1000;
		Settings.Topology.MinChambers = Settings.Topology.MaxChambers = 3;
		Settings.Topology.MinLoops = 0;
		Settings.Topology.MaxLoops = MAX_int32;
		Settings.Topology.EntranceCount = 1;
		Settings.Topology.MinTunnelRadiusCm = Settings.Topology.MaxTunnelRadiusCm = 100;
		Settings.Topology.MinChamberRadiusCm = Settings.Topology.MaxChamberRadiusCm = 200;
		Settings.Capsule.ClearanceMarginCm = 0;
		Settings.Limits.MaxCandidates = 3;
		Settings.Limits.MaxEdges = 3;
		Settings.Limits.MaxSegments = 4;
		Settings.Limits.MaxAttempts = 4;
		Settings.Limits.MaxValidationWork = 100000000;
		Settings.Water.Policy = EVoxelLandM5WaterPolicy::DryWorld;
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate;
		if (VoxelLand::M5::CandidatePlanner::Build(Settings, 42, {}, Budget.ToSharedRef(), Never, Candidate).Code
				!= EVoxelLandM5ErrorCode::None || !Candidate)
		{
			return false;
		}
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Out = Candidate;
		const auto Run = [&](const FVoxelLandM5CaveSettings& S, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> B,
			int64 Limit, TFunctionRef<bool()> Cancel) {
			return VoxelLand::M5::FiniteEntrancePlanner::Build(
				S, 42, Candidate.ToSharedRef(), *Window, nullptr, B, Limit, Cancel, Out);
		};
		auto D = Run(Settings, Budget.ToSharedRef(), 0, Never);
		bQueryReached = D.Code == EVoxelLandM5ErrorCode::BudgetExceeded && Out == Candidate;

		FVoxelLandM5CaveSettings Changed = Settings;
		Changed.Water.Policy = EVoxelLandM5WaterPolicy::Ignore;
		D = Run(Changed, Budget.ToSharedRef(), 1000000, Never);
		bWaterRejected = D.Code == EVoxelLandM5ErrorCode::Unproven && Out == Candidate;
		Changed = Settings;
		Changed.Protection.bRequireMask = true;
		D = Run(Changed, Budget.ToSharedRef(), 1000000, Never);
		bMaskRejected = D.Code == EVoxelLandM5ErrorCode::Unproven && Out == Candidate;
		Changed = Settings;
		Changed.PlanningMode = EVoxelLandM5PlanningMode::Regional;
		D = Run(Changed, Budget.ToSharedRef(), 1000000, Never);
		bModeRejected = D.Code == EVoxelLandM5ErrorCode::InvalidInput && Out == Candidate;
		D = Run(Settings, Budget.ToSharedRef(), -1, Never);
		bNegativeWork = D.Code == EVoxelLandM5ErrorCode::InvalidInput && Out == Candidate;
		D = Run(Settings, Budget.ToSharedRef(), 1000000, [] { return true; });
		bCancelled = D.Code == EVoxelLandM5ErrorCode::Cancelled && Out == Candidate;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Other;
		if (!FVoxelLandM5Budget::TryCreate(32 * 1024 * 1024, 100000000, Other))
		{
			return false;
		}
		D = Run(Settings, Other.ToSharedRef(), 1000000, Never);
		bForeignBudget = D.Code == EVoxelLandM5ErrorCode::InvalidInput && Out == Candidate;

		Changed = Settings;
		Changed.Protection.bProtectAll = true;
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> ProtectedCandidate;
		if (VoxelLand::M5::CandidatePlanner::Build(Changed, 42, {}, Budget.ToSharedRef(), Never, ProtectedCandidate).Code
				!= EVoxelLandM5ErrorCode::None || !ProtectedCandidate)
		{
			return false;
		}
		Out = ProtectedCandidate;
		D = VoxelLand::M5::FiniteEntrancePlanner::Build(Changed, 42, ProtectedCandidate.ToSharedRef(), *Window,
			nullptr, Budget.ToSharedRef(), 1000000, Never, Out);
		bProtected = D.Code == EVoxelLandM5ErrorCode::Blocked && Out == ProtectedCandidate;

		Changed = Settings;
		Changed.Topology.EntranceCount = 0;
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> NoEntrance;
		if (VoxelLand::M5::CandidatePlanner::Build(Changed, 42, {}, Budget.ToSharedRef(), Never, NoEntrance).Code
				!= EVoxelLandM5ErrorCode::None || !NoEntrance)
		{
			return false;
		}
		Out.Reset();
		D = VoxelLand::M5::FiniteEntrancePlanner::Build(
			Changed, 42, NoEntrance.ToSharedRef(), *Window, nullptr, Budget.ToSharedRef(), 1000000, Never, Out);
		bZeroEntrances = D.Code == EVoxelLandM5ErrorCode::None && Out.IsValid()
			&& Out->GetPlan().Entrances.IsEmpty();

		// Reuse the analytic entrance fixture's known geometry and seed, but query
		// the actual post-filter M4 window. Shift only Z to retain the same geometry
		// relative to its 50 cm midpoint surface.
		FVoxelLandM5CaveSettings Flat = Settings;
		Flat.Bounds.MinInclusive.Z = -950;
		Flat.Bounds.MaxExclusive.Z = 1050;
		Flat.Topology.MinChambers = Flat.Topology.MaxChambers = 1;
		Flat.Topology.MinLoops = Flat.Topology.MaxLoops = 0;
		const TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> FlatCandidate =
			FVoxelLandM5FiniteEntranceFixtureFactory::Create(Budget.ToSharedRef(), Flat);
		if (!FlatCandidate)
		{
			return false;
		}
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> FlatOut;
		D = VoxelLand::M5::FiniteEntrancePlanner::Build(
			Flat, 42, FlatCandidate.ToSharedRef(), *Window, nullptr, Budget.ToSharedRef(), 1000000, Never, FlatOut);
		bRealEntrance = D.Code == EVoxelLandM5ErrorCode::None && FlatOut.IsValid()
			&& FlatOut->GetPlan().Entrances.Num() == 1 && FlatOut->GetPlan().Segments.Num() == 2
			&& FlatOut->GetPlan().Connections.Num() == 1;
		if (bRealEntrance)
		{
			const auto& Approach = FlatOut->GetPlan().Segments[0];
			const auto& Ramp = FlatOut->GetPlan().Segments[1];
			// Independent seed-42 coordinates from the analytic fixture, shifted
			// vertically by the real window's 50 cm surface.
			bRealEntrance = Approach.EndCm.X == -195 && Approach.EndCm.Y == 167
				&& Approach.EndCm.Z == -50 && Ramp.StartCm.Z == -50 && Ramp.EndCm.Z == 235;
		}
		if (bRealEntrance)
		{
			// Center-relative proposals fit the 100 cm ideal tube for this capsule.
			// They do not establish floor support or final-field clearance.
			const auto ResolveFeet = [&Flat](const FVoxelLandM5PointCm& Center,
				TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> SharedBudget,
				TFunctionRef<bool()> Cancel, FVoxelLandM5PointCm& Feet) {
				FVoxelLandM5Diagnostic Result;
				if (Cancel())
				{
					Result.Code = EVoxelLandM5ErrorCode::Cancelled;
				}
				else if (!SharedBudget->TryChargeWork(1))
				{
					Result.Code = EVoxelLandM5ErrorCode::BudgetExceeded;
				}
				else
				{
					Feet = Center;
					Feet.Z -= Flat.Capsule.StandingHeightCm / 2;
				}
				return Result;
			};
			TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Routed;
			const auto RouteDiagnostic = VoxelLand::M5::FiniteRoutePlanner::Build(
				Flat, 42, FlatOut->GetPlan().Region, FlatOut.ToSharedRef(), Budget.ToSharedRef(),
				ResolveFeet, Never, Routed);
			RouteFailure = RouteDiagnostic.Message;
			if (RouteDiagnostic.Code == EVoxelLandM5ErrorCode::None && Routed
				&& Routed->GetPlan().Routes.Num() == 1 && Routed->GetPlan().Entrances.Num() == 1)
			{
				const auto& Route = Routed->GetPlan().Routes[0];
				const auto& Entrance = Routed->GetPlan().Entrances[0];
				bEntranceRoute = Route.Id == FlatOut->GetPlan().Connections[0].Id && Route.Legs.Num() == 2
					&& Route.Legs[0].Feature.Id == FlatOut->GetPlan().Segments[0].Id
					&& Route.Legs[1].Feature.Id == FlatOut->GetPlan().Segments[1].Id
					&& Entrance.bHasInsideFeetAnchor && Entrance.bHasOutsideFeetAnchor
					&& Entrance.InsideFeetAnchorCm.X == -195 && Entrance.InsideFeetAnchorCm.Y == 167
					&& Entrance.InsideFeetAnchorCm.Z == -142 && Entrance.OutsideFeetAnchorCm.X == 375
					&& Entrance.OutsideFeetAnchorCm.Y == 167 && Entrance.OutsideFeetAnchorCm.Z == 143;
			}
		}
		bInputUnchanged = FlatCandidate->GetPlan().Chambers.Num() == 1
			&& FlatCandidate->GetPlan().Chambers[0].CenterCm.Z == -140
			&& FlatCandidate->GetPlan().Entrances.IsEmpty() && FlatCandidate->GetPlan().Segments.IsEmpty()
			&& FlatCandidate->GetPlan().Connections.IsEmpty();
		return true;
	}).Get();
	TestTrue(TEXT("fixture setup"), bSetup);
	TestTrue(TEXT("zero-entrance candidate passes through"), bZeroEntrances);
	TestTrue(TEXT("real surface query limit propagates"), bQueryReached);
	TestTrue(TEXT("constant protection blocks openings"), bProtected);
	TestTrue(TEXT("unknown water fails closed"), bWaterRejected);
	TestTrue(TEXT("missing required mask fails closed"), bMaskRejected);
	TestTrue(TEXT("regional mode rejected"), bModeRejected);
	TestTrue(TEXT("foreign budget rejected"), bForeignBudget);
	TestTrue(TEXT("negative query limit rejected"), bNegativeWork);
	TestTrue(TEXT("cancellation preserves output"), bCancelled);
	TestTrue(TEXT("real M4 window produces one finite entrance"), bRealEntrance);
	TestTrue(FString::Printf(TEXT("real M4 entrance produces its nominal route and anchors: %s"), *RouteFailure), bEntranceRoute);
	TestTrue(TEXT("entrance construction preserves candidate input"), bInputUnchanged);
	return bSetup;
}

#endif
