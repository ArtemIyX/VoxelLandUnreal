#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Data/Structs/VoxelLandM5StageManifest.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Generation/VoxelLandM5BaselineWindow.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5CandidatePlanner.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5FiniteRoutePlanner.h"
#include "Generation/VoxelLandM5ProtectionGrid.h"
#include "Generation/VoxelLandM5ScratchChunks.h"
#include "Generation/VoxelLandM5ScratchRouteChecks.h"
#include "Misc/AutomationTest.h"

class FVoxelLandM5ScratchRouteLineageFixture final
{
public:
	static TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Make(
		const FVoxelLandM5CaveSettings& Settings, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget)
	{
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
		if (!Budget->TryReserveBytes(65536, Lease))
		{
			return nullptr;
		}
		FVoxelLandM5CavePlan Plan;
		Plan.Bounds = Settings.Bounds;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			FVoxelLandM5Chamber Chamber;
			Chamber.bHasId = true;
			Chamber.Id = uint64(Index);
			Chamber.CenterCm.X = -300 + Index * 300;
			Chamber.CenterCm.Y = -200;
			Chamber.CenterCm.Z = -200;
			Chamber.RadiusCm = 200;
			Plan.Chambers.Add(Chamber);
		}
		for (int32 Index = 0; Index < 2; ++Index)
		{
			FVoxelLandM5Connection Connection;
			Connection.bHasId = true;
			Connection.Id = uint64(7 + Index);
			Connection.Start.bValid = Connection.End.bValid = true;
			Connection.Start.Id = uint64(Index);
			Connection.End.Id = uint64(Index + 1);
			Plan.Connections.Add(Connection);
			FVoxelLandM5Segment Segment;
			Segment.bHasId = Segment.bHasConnectionId = true;
			Segment.Id = uint64(9 + Index);
			Segment.ConnectionId = Connection.Id;
			Segment.StartCm = Plan.Chambers[Index].CenterCm;
			Segment.EndCm = Plan.Chambers[Index + 1].CenterCm;
			Segment.RadiusCm = 100;
			Plan.Segments.Add(Segment);
		}
		const FVoxelLandM5RegionCoord Region;
		return MakeShareable(new FVoxelLandM5CandidateResult(Budget, Settings, 42, Region, MoveTemp(Plan), MoveTemp(Lease)));
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5ScratchRouteChecksTest, "VoxelLand.M5.ScratchRouteChecks.Finite",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5ScratchRouteChecksTest::RunTest(const FString& Parameters)
{
	bool bDelegated = false, bSeed = false, bManifest = false, bSettings = false;
	bool bWindow = false, bCandidate = false, bMask = false, bMode = false;
	bool bForeign = false, bCancelled = false, bMidCancelled = false, bExhausted = false;
	bool bLineage = false, bUnrelated = false, bSame = false, bSourceLifetime = false;
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
		Candidate = FVoxelLandM5ScratchRouteLineageFixture::Make(Settings, Budget.ToSharedRef());
		OtherCandidate = FVoxelLandM5ScratchRouteLineageFixture::Make(Settings, Budget.ToSharedRef());
		if (!Candidate || !OtherCandidate)
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
		const auto Resolve = [](const FVoxelLandM5PointCm& Center,
			TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> SharedBudget, TFunctionRef<bool()> Cancel,
			FVoxelLandM5PointCm& Feet) {
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
				Feet.Z -= 92;
			}
			return Result;
		};
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Routed;
		const FVoxelLandM5RegionCoord Region;
		const FVoxelLandM5Diagnostic Built = VoxelLand::M5::FiniteRoutePlanner::Build(
			Settings, 42, Region, Candidate.ToSharedRef(), Budget.ToSharedRef(), Resolve, Never, Routed);
		if (Built.Code != EVoxelLandM5ErrorCode::None || !Routed)
		{
			return false;
		}
		D = VoxelLand::M5::ScratchRouteChecks::CheckAll(Settings, 42, Manifest, Window.ToSharedRef(),
			Candidate.ToSharedRef(), Routed.ToSharedRef(), nullptr, Chunks.ToSharedRef(), 0, Selections, Budget.ToSharedRef(), Never);
		bLineage = Routed->IsDirectlyDerivedFrom(*Candidate) && !Routed->IsDirectlyDerivedFrom(*OtherCandidate)
			&& D.Code == EVoxelLandM5ErrorCode::InvalidInput && D.Stage == FName(TEXT("M5.MultiRouteChecks"));
		D = VoxelLand::M5::ScratchRouteChecks::CheckAll(Settings, 42, Manifest, Window.ToSharedRef(),
			Candidate.ToSharedRef(), OtherCandidate.ToSharedRef(), nullptr, Chunks.ToSharedRef(), 0, Selections, Budget.ToSharedRef(), Never);
		bUnrelated = D.Code == EVoxelLandM5ErrorCode::InvalidInput && D.Stage == FName(TEXT("M5.ScratchRouteChecks"));
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> OtherRouted;
		if (VoxelLand::M5::FiniteRoutePlanner::Build(Settings, 42, Region, OtherCandidate.ToSharedRef(),
				Budget.ToSharedRef(), Resolve, Never, OtherRouted).Code != EVoxelLandM5ErrorCode::None || !OtherRouted)
		{
			return false;
		}
		D = VoxelLand::M5::ScratchRouteChecks::CheckAll(Settings, 42, Manifest, Window.ToSharedRef(),
			Candidate.ToSharedRef(), OtherRouted.ToSharedRef(), nullptr, Chunks.ToSharedRef(), 0, Selections, Budget.ToSharedRef(), Never);
		bUnrelated &= D.Code == EVoxelLandM5ErrorCode::InvalidInput && D.Stage == FName(TEXT("M5.ScratchRouteChecks"));
		D = VoxelLand::M5::ScratchRouteChecks::CheckAll(Settings, 42, Manifest, Window.ToSharedRef(),
			Candidate.ToSharedRef(), Candidate.ToSharedRef(), nullptr, Chunks.ToSharedRef(), 0, Selections, Budget.ToSharedRef(), Never);
		bSame = D.Code == EVoxelLandM5ErrorCode::InvalidInput && D.Stage == FName(TEXT("M5.MultiRouteChecks"));
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Temporary =
			FVoxelLandM5ScratchRouteLineageFixture::Make(Settings, Budget.ToSharedRef());
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> TemporaryRouted;
		if (!Temporary || VoxelLand::M5::FiniteRoutePlanner::Build(Settings, 42, Region, Temporary.ToSharedRef(),
				Budget.ToSharedRef(), Resolve, Never, TemporaryRouted).Code != EVoxelLandM5ErrorCode::None || !TemporaryRouted)
		{
			return false;
		}
		TWeakPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> WeakSource = Temporary;
		Temporary.Reset();
		bSourceLifetime = WeakSource.IsValid();
		TemporaryRouted.Reset();
		bSourceLifetime &= !WeakSource.IsValid();
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
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> ForeignRouted =
			FVoxelLandM5ScratchRouteLineageFixture::Make(Settings, OtherBudget.ToSharedRef());
		if (!ForeignRouted)
		{
			return false;
		}
		D = VoxelLand::M5::ScratchRouteChecks::CheckAll(Settings, 42, Manifest, Window.ToSharedRef(),
			Candidate.ToSharedRef(), ForeignRouted.ToSharedRef(), nullptr, Chunks.ToSharedRef(), 0, Selections, Budget.ToSharedRef(), Never);
		bForeign &= D.Code == EVoxelLandM5ErrorCode::InvalidInput && D.Stage == FName(TEXT("M5.ScratchRouteChecks"));
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
	TestTrue(TEXT("direct routed candidate reaches CPU route validator"), bLineage);
	TestTrue(TEXT("equal but unrelated candidate rejected"), bUnrelated);
	TestTrue(TEXT("same candidate overload remains compatible"), bSame);
	TestTrue(TEXT("routed result retains and releases direct source"), bSourceLifetime);
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
