#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Data/Structs/VoxelLandM5StageManifest.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Generation/VoxelLandM5BaselineWindow.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5CandidatePlanner.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5ScratchChunks.h"
#include "Generation/VoxelLandM5ScratchRoofCheck.h"
#include "Misc/AutomationTest.h"

class FVoxelLandM5ScratchRoofFixtureFactory final
{
public:
	static int16 ChangeSample(const TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe>& Source)
	{
		auto Mutable = ConstCastSharedPtr<FVoxelLandM5ScratchChunks>(Source);
		int16& Sample = Mutable->Chunks[0].Density[10 + 35 * (10 + 35)];
		const int16 Previous = Sample;
		Sample = Previous == MAX_int16 ? Previous - 1 : Previous + 1;
		return Previous;
	}
	static void RestoreSample(const TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe>& Source, int16 Value)
	{
		auto Mutable = ConstCastSharedPtr<FVoxelLandM5ScratchChunks>(Source);
		Mutable->Chunks[0].Density[10 + 35 * (10 + 35)] = Value;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5ScratchRoofCheckTest, "VoxelLand.M5.ScratchRoof.Preserved",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5ScratchRoofCheckTest::RunTest(const FString& Parameters)
{
	bool bPreserved = false, bBlocked = false, bCancelled = false, bChangedSettings = false, bForeign = false;
	const bool bSetup = Async(EAsyncExecution::ThreadPool, [&]() {
		const auto Never = [] { return false; };
		FVoxelLandProgramDraft Draft;
		Draft.RegisterCount = 1;
		FVoxelLandInstruction Input, Store;
		Input.Op = EVoxelLandOpcode::LoadZ;
		Store.Op = EVoxelLandOpcode::StoreDensity;
		Draft.Instructions = {Input, Store};
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (FVoxelLandProgram::Validate(Draft, Program) || !Program) { return false; }
		TArray<int32> Heights;
		Heights.Init(50 * 256, 129 * 129);
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
		if (!FVoxelLandM4HeightGrid::Create(-5000, -5000, 128, 128, 100, Heights, Grid)) { return false; }
		FVoxelLandGeneratorSettings G;
		G.Program = Program;
		G.Profile.VoxelSizeCm = 100.f;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
		if (FVoxelLandGenerator::CreateM4HeightField(G, Grid, false, Generator) || !Generator) { return false; }
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
		if (!FVoxelLandM5Budget::TryCreate(256 * 1024 * 1024, 1000000000, Budget)) { return false; }
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Window;
		if (FVoxelLandM5BaselineWindow::Build(Generator.ToSharedRef(), FIntVector(-1, -1, -1),
			FIntVector(2, 2, 2), 27, Budget.ToSharedRef(), Never, Window).Code != EVoxelLandM5ErrorCode::None
			|| !Window) { return false; }
		FVoxelLandM5CaveSettings S;
		S.bEnabled = true;
		S.StageId = FGuid(1, 2, 3, 4);
		S.Bounds.MinInclusive.X = S.Bounds.MinInclusive.Y = S.Bounds.MinInclusive.Z = -3000;
		S.Bounds.MaxExclusive.X = S.Bounds.MaxExclusive.Y = S.Bounds.MaxExclusive.Z = 3001;
		S.Topology.MinChambers = S.Topology.MaxChambers = 3;
		S.Topology.MinLoops = 0;
		S.Topology.MaxLoops = 3;
		S.Topology.EntranceCount = 0;
		S.Topology.MinTunnelRadiusCm = S.Topology.MaxTunnelRadiusCm = 100;
		S.Topology.MinChamberRadiusCm = S.Topology.MaxChamberRadiusCm = 200;
		S.Capsule.ClearanceMarginCm = 0;
		S.RoofThicknessCm = 0;
		S.Protection.bProtectAll = true;
		S.Limits.MaxCandidates = 3;
		S.Limits.MaxEdges = 3;
		S.Limits.MaxSegments = 4;
		S.Limits.MaxAttempts = 4;
		S.Limits.MaxBytes = 256 * 1024 * 1024;
		S.Limits.MaxValidationWork = 1000000000;
		S.Water.Policy = EVoxelLandM5WaterPolicy::DryWorld;
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate;
		if (VoxelLand::M5::CandidatePlanner::Build(S, 42, {}, Budget.ToSharedRef(), Never, Candidate).Code
			!= EVoxelLandM5ErrorCode::None || !Candidate) { return false; }
		FVoxelLandM5StageManifest Manifest;
		TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Chunks;
		if (FVoxelLandM5ScratchChunks::Build(S, Manifest, 42, Window.ToSharedRef(), Candidate.ToSharedRef(),
			nullptr, FIntVector(0, 0, 0), FIntVector(1, 1, 1), 1, Budget.ToSharedRef(), Never, Chunks).Code
			!= EVoxelLandM5ErrorCode::None || !Chunks) { return false; }
		using namespace VoxelLand::M5::ScratchRoofCheck;
		bPreserved = Check(S, 42, Manifest, Window.ToSharedRef(), Candidate.ToSharedRef(), nullptr,
			Chunks.ToSharedRef(), Budget.ToSharedRef(), Never).Code == EVoxelLandM5ErrorCode::None;
		const int16 Original = FVoxelLandM5ScratchRoofFixtureFactory::ChangeSample(Chunks);
		bBlocked = Check(S, 42, Manifest, Window.ToSharedRef(), Candidate.ToSharedRef(), nullptr,
			Chunks.ToSharedRef(), Budget.ToSharedRef(), Never).Code == EVoxelLandM5ErrorCode::Blocked;
		FVoxelLandM5ScratchRoofFixtureFactory::RestoreSample(Chunks, Original);
		bCancelled = Check(S, 42, Manifest, Window.ToSharedRef(), Candidate.ToSharedRef(), nullptr,
			Chunks.ToSharedRef(), Budget.ToSharedRef(), [] { return true; }).Code == EVoxelLandM5ErrorCode::Cancelled;
		FVoxelLandM5CaveSettings Changed = S;
		Changed.RoofThicknessCm = 100;
		bChangedSettings = Check(Changed, 42, Manifest, Window.ToSharedRef(), Candidate.ToSharedRef(), nullptr,
			Chunks.ToSharedRef(), Budget.ToSharedRef(), Never).Code == EVoxelLandM5ErrorCode::InvalidInput;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Other;
		if (!FVoxelLandM5Budget::TryCreate(256 * 1024 * 1024, 1000000000, Other)) { return false; }
		bForeign = Check(S, 42, Manifest, Window.ToSharedRef(), Candidate.ToSharedRef(), nullptr,
			Chunks.ToSharedRef(), Other.ToSharedRef(), Never).Code == EVoxelLandM5ErrorCode::InvalidInput;
		return true;
	}).Get();
	TestTrue(TEXT("fixture setup"), bSetup);
	TestTrue(TEXT("baseline roof samples remain unchanged"), bPreserved);
	TestTrue(TEXT("changed roof influence is blocked"), bBlocked);
	TestTrue(TEXT("cancellation fails"), bCancelled);
	TestTrue(TEXT("changed roof settings fail"), bChangedSettings);
	TestTrue(TEXT("foreign budget fails"), bForeign);
	return bSetup;
}

#endif
