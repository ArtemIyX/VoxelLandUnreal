#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Data/Caves/Persistence/VoxelLandCaveStageManifest.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightGrid.h"
#include "Generation/Caves/Field/VoxelLandCaveBaselineWindow.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Planning/VoxelLandCaveCandidatePlanner.h"
#include "Generation/Caves/Planning/VoxelLandCaveCandidateResult.h"
#include "Generation/Caves/Field/VoxelLandCaveProtectionGrid.h"
#include "Generation/Caves/Field/VoxelLandCaveScratchChunks.h"
#include "Generation/Caves/Field/VoxelLandCaveScratchProtectionCheck.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5ScratchProtectionCheckTest, "VoxelLand.M5.ScratchProtection.Cells",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5ScratchProtectionCheckTest::RunTest(const FString& Parameters)
{
	bool bEmpty = false, bBlocked = false, bMissing = false, bCoverage = false, bCancelled = false, bForeign = false;
	bool bMatchingGrid = false, bOmittedGrid = false, bDifferentGrid = false, bRequireChanged = false;
	bool bProtectAllChanged = false, bDifferentWindow = false;
	const bool bSetup = Async(EAsyncExecution::ThreadPool, [&]() {
		const auto Never = [] {
			return false;
		};
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
		FVoxelLandGeneratorSettings G;
		G.Program = Program;
		G.Profile.VoxelSizeCm = 100.f;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
		if (FVoxelLandGenerator::CreateM4HeightField(G, Grid, false, Generator) || !Generator)
		{
			return false;
		}
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
		if (!FVoxelLandM5Budget::TryCreate(256 * 1024 * 1024, 1000000000, Budget))
		{
			return false;
		}
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Window;
		if (FVoxelLandM5BaselineWindow::Build(Generator.ToSharedRef(), FIntVector(-1, -1, -1), FIntVector(2, 2, 2), 27,
				Budget.ToSharedRef(), Never, Window)
					.Code
				!= EVoxelLandM5ErrorCode::None
			|| !Window)
		{
			return false;
		}
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
		S.Limits.MaxCandidates = 3;
		S.Limits.MaxEdges = 3;
		S.Limits.MaxSegments = 4;
		S.Limits.MaxAttempts = 4;
		S.Limits.MaxBytes = 256 * 1024 * 1024;
		S.Limits.MaxValidationWork = 1000000000;
		S.Water.Policy = EVoxelLandM5WaterPolicy::DryWorld;
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate;
		if (VoxelLand::M5::CandidatePlanner::Build(S, 42, {}, Budget.ToSharedRef(), Never, Candidate).Code
				!= EVoxelLandM5ErrorCode::None
			|| !Candidate)
		{
			return false;
		}
		TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Chunks;
		if (FVoxelLandM5ScratchChunks::Build(S, FVoxelLandM5StageManifest{}, 42, Window.ToSharedRef(),
				Candidate.ToSharedRef(), nullptr, FIntVector(0, 0, 0), FIntVector(1, 1, 1), 1, Budget.ToSharedRef(),
				Never, Chunks)
					.Code
				!= EVoxelLandM5ErrorCode::None
			|| !Chunks)
		{
			return false;
		}
		using namespace VoxelLand::M5::ScratchProtectionCheck;
		bEmpty = Check(*Window, *Chunks, nullptr, false, false, Budget.ToSharedRef(), Never).Code
			  == EVoxelLandM5ErrorCode::None;
		bMissing = Check(*Window, *Chunks, nullptr, true, false, Budget.ToSharedRef(), Never).Code
				== EVoxelLandM5ErrorCode::Unproven;
		bCancelled = Check(*Window, *Chunks, nullptr, false, false, Budget.ToSharedRef(), [] { return true; }).Code
				  == EVoxelLandM5ErrorCode::Cancelled;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Other;
		if (!FVoxelLandM5Budget::TryCreate(256 * 1024 * 1024, 1000000000, Other))
		{
			return false;
		}
		bForeign = Check(*Window, *Chunks, nullptr, false, false, Other.ToSharedRef(), Never).Code
				== EVoxelLandM5ErrorCode::InvalidInput;
		// Full window samples begin at -3300 cm; the extra cell on each
		// side supplies the complete projection halo. Cell (0,0,0) is
		// index (34,34,34), adjacent to chunk sample (0,0,0).
		TArray<uint8> Cells;
		Cells.Init(0, 100 * 100 * 100);
		Cells[34 + 100 * (34 + 100 * 34)] = 1;
		TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> Protection;
		FVoxelLandM5PointCm GridOrigin;
		GridOrigin.X = GridOrigin.Y = GridOrigin.Z = -3400;
		if (FVoxelLandM5ProtectionGrid::Capture(
				FIntVector(100), GridOrigin, 100, Cells, Budget.ToSharedRef(), Never, Protection)
					.Code
				!= EVoxelLandM5ErrorCode::None
			|| !Protection)
		{
			return false;
		}
		TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> ProtectedChunks;
		if (FVoxelLandM5ScratchChunks::Build(S, FVoxelLandM5StageManifest{}, 42, Window.ToSharedRef(),
				Candidate.ToSharedRef(), Protection, FIntVector(0, 0, 0), FIntVector(1, 1, 1), 1, Budget.ToSharedRef(),
				Never, ProtectedChunks)
					.Code
				!= EVoxelLandM5ErrorCode::None
			|| !ProtectedChunks)
		{
			return false;
		}
		bMatchingGrid =
			Check(*Window, *ProtectedChunks, Protection.Get(), false, false, Budget.ToSharedRef(), Never).Code
			== EVoxelLandM5ErrorCode::None;
		bOmittedGrid = Check(*Window, *ProtectedChunks, nullptr, false, false, Budget.ToSharedRef(), Never).Code
					== EVoxelLandM5ErrorCode::InvalidInput;
		bRequireChanged =
			Check(*Window, *ProtectedChunks, Protection.Get(), true, false, Budget.ToSharedRef(), Never).Code
			== EVoxelLandM5ErrorCode::InvalidInput;
		bProtectAllChanged =
			Check(*Window, *ProtectedChunks, Protection.Get(), false, true, Budget.ToSharedRef(), Never).Code
			== EVoxelLandM5ErrorCode::InvalidInput;
		TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> OtherProtection;
		if (FVoxelLandM5ProtectionGrid::Capture(
				FIntVector(100), GridOrigin, 100, Cells, Budget.ToSharedRef(), Never, OtherProtection)
					.Code
				!= EVoxelLandM5ErrorCode::None
			|| !OtherProtection)
		{
			return false;
		}
		bDifferentGrid =
			Check(*Window, *ProtectedChunks, OtherProtection.Get(), false, false, Budget.ToSharedRef(), Never).Code
			== EVoxelLandM5ErrorCode::InvalidInput;
		TArray<uint8> ShortCells;
		ShortCells.Init(0, 31 * 32 * 32);
		TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> Short;
		if (FVoxelLandM5ProtectionGrid::Capture(
				FIntVector(31, 32, 32), GridOrigin, 100, ShortCells, Budget.ToSharedRef(), Never, Short)
					.Code
				!= EVoxelLandM5ErrorCode::None
			|| !Short)
		{
			return false;
		}
		bCoverage = Check(*Window, *ProtectedChunks, Short.Get(), false, false, Budget.ToSharedRef(), Never).Code
				 == EVoxelLandM5ErrorCode::InvalidInput;
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> OtherWindow;
		if (FVoxelLandM5BaselineWindow::Build(Generator.ToSharedRef(), FIntVector(-1, -1, -1), FIntVector(1, 1, 1), 8,
				Budget.ToSharedRef(), Never, OtherWindow)
					.Code
				!= EVoxelLandM5ErrorCode::None
			|| !OtherWindow)
		{
			return false;
		}
		bDifferentWindow =
			Check(*OtherWindow, *ProtectedChunks, Protection.Get(), false, false, Budget.ToSharedRef(), Never).Code
			== EVoxelLandM5ErrorCode::InvalidInput;
		TSharedPtr<FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Mutable =
			ConstCastSharedPtr<FVoxelLandM5ScratchChunks>(ProtectedChunks);
		const int16 Original = Mutable->Chunks[0].Density[0];
		Mutable->Chunks[0].Density[0] = Original == MAX_int16 ? Original - 1 : Original + 1;
		bBlocked = Check(*Window, *ProtectedChunks, Protection.Get(), false, false, Budget.ToSharedRef(), Never).Code
				== EVoxelLandM5ErrorCode::Blocked;
		return true;
	}).Get();
	TestTrue(TEXT("fixture setup"), bSetup);
	TestTrue(TEXT("omitted mask adds no protection"), bEmpty);
	TestTrue(TEXT("changed sample influencing protected cell blocked"), bBlocked);
	TestTrue(TEXT("required absent mask fails"), bMissing);
	TestTrue(TEXT("matching protection grid checks"), bMatchingGrid);
	TestTrue(TEXT("omitting source grid fails"), bOmittedGrid);
	TestTrue(TEXT("different grid fails even with equal cells"), bDifferentGrid);
	TestTrue(TEXT("incomplete foreign grid fails"), bCoverage);
	TestTrue(TEXT("changed require-mask flag fails"), bRequireChanged);
	TestTrue(TEXT("changed protect-all flag fails"), bProtectAllChanged);
	TestTrue(TEXT("different baseline window fails"), bDifferentWindow);
	TestTrue(TEXT("cancellation fails"), bCancelled);
	TestTrue(TEXT("foreign budget fails"), bForeign);
	return bSetup;
}

#endif
