#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Data/Structs/VoxelLandM5StageManifest.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Generation/VoxelLandM5BaselineWindow.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5CandidatePlanner.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5ProtectionGrid.h"
#include "Generation/VoxelLandM5ScratchChunks.h"
#include "Generation/VoxelLandM5ScratchRouteChecks.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5ScratchRouteChecksTest, "VoxelLand.M5.ScratchRouteChecks.Finite",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5ScratchRouteChecksTest::RunTest(const FString& Parameters)
{
	bool bDelegated = false, bSeed = false, bManifest = false, bSettings = false;
	bool bWindow = false, bCandidate = false, bMask = false, bMode = false;
	bool bForeign = false, bCancelled = false, bMidCancelled = false, bExhausted = false;
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
		Heights.Init(50 * 256, 129 * 129);
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
		if (!FVoxelLandM4HeightGrid::Create(-5000, -5000, 128, 128, 100, Heights, Grid))
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
		if (!FVoxelLandM5Budget::TryCreate(256 * 1024 * 1024, 1000000000, Budget))
		{
			return false;
		}
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Window;
		if (FVoxelLandM5BaselineWindow::Build(Generator.ToSharedRef(), FIntVector(-1, -1, -1),
				FIntVector(2, 2, 2), 27, Budget.ToSharedRef(), Never, Window).Code != EVoxelLandM5ErrorCode::None
			|| !Window)
		{
			return false;
		}
		FVoxelLandM5CaveSettings Settings;
		Settings.bEnabled = true;
		Settings.StageId = FGuid(1, 2, 3, 4);
		Settings.Bounds.MinInclusive.X = Settings.Bounds.MinInclusive.Y = Settings.Bounds.MinInclusive.Z = -3000;
		Settings.Bounds.MaxExclusive.X = Settings.Bounds.MaxExclusive.Y = Settings.Bounds.MaxExclusive.Z = 3001;
		Settings.Topology.MinChambers = Settings.Topology.MaxChambers = 3;
		Settings.Topology.MinLoops = 0;
		Settings.Topology.MaxLoops = 3;
		Settings.Topology.EntranceCount = 0;
		Settings.Topology.MinTunnelRadiusCm = Settings.Topology.MaxTunnelRadiusCm = 100;
		Settings.Topology.MinChamberRadiusCm = Settings.Topology.MaxChamberRadiusCm = 200;
		Settings.Capsule.ClearanceMarginCm = 0;
		Settings.RoofThicknessCm = 0;
		Settings.Limits.MaxCandidates = 3;
		Settings.Limits.MaxEdges = 3;
		Settings.Limits.MaxSegments = 4;
		Settings.Limits.MaxAttempts = 4;
		Settings.Limits.MaxBytes = 256 * 1024 * 1024;
		Settings.Limits.MaxValidationWork = 1000000000;
		Settings.Water.Policy = EVoxelLandM5WaterPolicy::DryWorld;
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate;
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> OtherCandidate;
		if (VoxelLand::M5::CandidatePlanner::Build(Settings, 42, {}, Budget.ToSharedRef(), Never, Candidate).Code
				!= EVoxelLandM5ErrorCode::None || !Candidate
			|| VoxelLand::M5::CandidatePlanner::Build(Settings, 42, {}, Budget.ToSharedRef(), Never, OtherCandidate).Code
				!= EVoxelLandM5ErrorCode::None || !OtherCandidate)
		{
			return false;
		}
		const FVoxelLandM5StageManifest Manifest;
		TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Chunks;
		if (FVoxelLandM5ScratchChunks::Build(Settings, Manifest, 42, Window.ToSharedRef(), Candidate.ToSharedRef(),
				nullptr, FIntVector(0, 0, 0), FIntVector(1, 1, 1), 1, Budget.ToSharedRef(), Never, Chunks).Code
				!= EVoxelLandM5ErrorCode::None || !Chunks)
		{
			return false;
		}
		TArray<FVoxelLandM5MultiRouteSupportSelection> Selections;
		const auto Run = [&](const FVoxelLandM5CaveSettings& S, int64 Seed, const FVoxelLandM5StageManifest& M,
			TSharedRef<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> W,
			TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> C,
			TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> P,
			TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> B, TFunctionRef<bool()> Cancel) {
			return VoxelLand::M5::ScratchRouteChecks::CheckAll(
				S, Seed, M, W, C, P, Chunks.ToSharedRef(), 0, Selections, B, Cancel);
		};
		auto D = Run(Settings, 42, Manifest, Window.ToSharedRef(), Candidate.ToSharedRef(), nullptr,
			Budget.ToSharedRef(), Never);
		bDelegated = D.Code == EVoxelLandM5ErrorCode::InvalidInput && D.Stage == FName(TEXT("M5.MultiRouteChecks"));
		D = Run(Settings, 43, Manifest, Window.ToSharedRef(), Candidate.ToSharedRef(), nullptr, Budget.ToSharedRef(), Never);
		bSeed = D.Code == EVoxelLandM5ErrorCode::InvalidInput && D.Stage == FName(TEXT("M5.ScratchRouteChecks"));
		FVoxelLandM5StageManifest ChangedManifest = Manifest;
		++ChangedManifest.Composition;
		D = Run(Settings, 42, ChangedManifest, Window.ToSharedRef(), Candidate.ToSharedRef(), nullptr, Budget.ToSharedRef(), Never);
		bManifest = D.Code == EVoxelLandM5ErrorCode::InvalidInput && D.Stage == FName(TEXT("M5.ScratchRouteChecks"));
		FVoxelLandM5CaveSettings Changed = Settings;
		Changed.RoofThicknessCm += 1;
		D = Run(Changed, 42, Manifest, Window.ToSharedRef(), Candidate.ToSharedRef(), nullptr, Budget.ToSharedRef(), Never);
		bSettings = D.Code == EVoxelLandM5ErrorCode::InvalidInput && D.Stage == FName(TEXT("M5.ScratchRouteChecks"));
		D = Run(Settings, 42, Manifest, Window.ToSharedRef(), OtherCandidate.ToSharedRef(), nullptr, Budget.ToSharedRef(), Never);
		bCandidate = D.Code == EVoxelLandM5ErrorCode::InvalidInput && D.Stage == FName(TEXT("M5.ScratchRouteChecks"));

		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> OtherWindow;
		if (FVoxelLandM5BaselineWindow::Build(Generator.ToSharedRef(), FIntVector(0, 0, 0), FIntVector(1, 1, 1),
				1, Budget.ToSharedRef(), Never, OtherWindow).Code != EVoxelLandM5ErrorCode::None || !OtherWindow)
		{
			return false;
		}
		D = Run(Settings, 42, Manifest, OtherWindow.ToSharedRef(), Candidate.ToSharedRef(), nullptr, Budget.ToSharedRef(), Never);
		bWindow = D.Code == EVoxelLandM5ErrorCode::InvalidInput && D.Stage == FName(TEXT("M5.ScratchRouteChecks"));
		TArray<uint8> Cells;
		Cells.Add(0);
		TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> OtherMask;
		if (FVoxelLandM5ProtectionGrid::Capture(FIntVector(1), FVoxelLandM5PointCm{}, 100, Cells, Budget.ToSharedRef(),
				Never, OtherMask).Code != EVoxelLandM5ErrorCode::None || !OtherMask)
		{
			return false;
		}
		D = Run(Settings, 42, Manifest, Window.ToSharedRef(), Candidate.ToSharedRef(), OtherMask, Budget.ToSharedRef(), Never);
		bMask = D.Code == EVoxelLandM5ErrorCode::InvalidInput && D.Stage == FName(TEXT("M5.ScratchRouteChecks"));
		Changed = Settings;
		Changed.PlanningMode = EVoxelLandM5PlanningMode::Regional;
		D = Run(Changed, 42, Manifest, Window.ToSharedRef(), Candidate.ToSharedRef(), nullptr, Budget.ToSharedRef(), Never);
		bMode = D.Code == EVoxelLandM5ErrorCode::InvalidInput && D.Stage == FName(TEXT("M5.ScratchRouteChecks"));
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> OtherBudget;
		if (!FVoxelLandM5Budget::TryCreate(256 * 1024 * 1024, 1000000000, OtherBudget))
		{
			return false;
		}
		D = Run(Settings, 42, Manifest, Window.ToSharedRef(), Candidate.ToSharedRef(), nullptr, OtherBudget.ToSharedRef(), Never);
		bForeign = D.Code == EVoxelLandM5ErrorCode::InvalidInput && D.Stage == FName(TEXT("M5.ScratchRouteChecks"));
		D = Run(Settings, 42, Manifest, Window.ToSharedRef(), Candidate.ToSharedRef(), nullptr, Budget.ToSharedRef(),
			[] { return true; });
		bCancelled = D.Code == EVoxelLandM5ErrorCode::Cancelled;
		int32 Polls = 0;
		const auto CancelAfterCheck = [&Polls] { return ++Polls >= 2; };
		D = Run(Settings, 42, Manifest, Window.ToSharedRef(), Candidate.ToSharedRef(), nullptr, Budget.ToSharedRef(),
			CancelAfterCheck);
		bMidCancelled = D.Code == EVoxelLandM5ErrorCode::Cancelled && D.Stage == FName(TEXT("M5.ScratchRouteChecks"));
		int64 MaxBytes = 0, MaxWork = 0, LiveBytes = 0, PeakBytes = 0, UsedWork = 0;
		Budget->GetLimits(MaxBytes, MaxWork);
		Budget->GetUsage(LiveBytes, PeakBytes, UsedWork);
		if (!Budget->TryChargeWork(MaxWork - UsedWork))
		{
			return false;
		}
		D = Run(Settings, 42, Manifest, Window.ToSharedRef(), Candidate.ToSharedRef(), nullptr, Budget.ToSharedRef(), Never);
		bExhausted = D.Code == EVoxelLandM5ErrorCode::BudgetExceeded && D.Stage == FName(TEXT("M5.ScratchRouteChecks"));
		return true;
	}).Get();

	TestTrue(TEXT("fixture setup"), bSetup);
	TestTrue(TEXT("matching inputs reach CPU route validator"), bDelegated);
	TestTrue(TEXT("changed seed rejected"), bSeed);
	TestTrue(TEXT("changed manifest rejected"), bManifest);
	TestTrue(TEXT("changed settings rejected"), bSettings);
	TestTrue(TEXT("different baseline rejected"), bWindow);
	TestTrue(TEXT("different candidate rejected"), bCandidate);
	TestTrue(TEXT("different protection rejected"), bMask);
	TestTrue(TEXT("regional mode rejected"), bMode);
	TestTrue(TEXT("foreign budget rejected"), bForeign);
	TestTrue(TEXT("cancellation rejected"), bCancelled);
	TestTrue(TEXT("cancellation after input checks rejected"), bMidCancelled);
	TestTrue(TEXT("exhausted work rejected before delegation"), bExhausted);
	return bSetup;
}

#endif
