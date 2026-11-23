#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Generation/VoxelLandM5BaselineWindow.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5CandidatePlanner.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5FiniteEntrancePlanner.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5FiniteEntrancePlannerTest, "VoxelLand.M5.FiniteEntrancePlanner.Window",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5FiniteEntrancePlannerTest::RunTest(const FString& Parameters)
{
	bool bZeroEntrances = false, bQueryReached = false, bProtected = false;
	bool bWaterRejected = false, bMaskRejected = false, bModeRejected = false;
	bool bForeignBudget = false, bCancelled = false, bNegativeWork = false;
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
	return bSetup;
}

#endif
