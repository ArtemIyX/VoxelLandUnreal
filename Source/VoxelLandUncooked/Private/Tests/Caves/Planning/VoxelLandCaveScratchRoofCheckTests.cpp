#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Data/Caves/Persistence/VoxelLandCaveStageManifest.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightGrid.h"
#include "Generation/Caves/Field/VoxelLandCaveBaselineWindow.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Planning/VoxelLandCaveCandidatePlanner.h"
#include "Generation/Caves/Planning/VoxelLandCaveCandidateResult.h"
#include "Generation/Caves/Field/VoxelLandCaveScratchChunks.h"
#include "Generation/Caves/Planning/VoxelLandCaveScratchRoofCheck.h"
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
	static void RestoreSample(
		const TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe>& Source, int16 Value)
	{
		auto Mutable = ConstCastSharedPtr<FVoxelLandM5ScratchChunks>(Source);
		Mutable->Chunks[0].Density[10 + 35 * (10 + 35)] = Value;
	}

	// The chunk sample (10,10,20) is far above the flat Z=0 surface.
	// Mutating both copies is test-only; all retained source identities stay fixed.
	static bool SetIsolatedZero(const TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe>& Window,
		const TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe>& Source, int16& Previous)
	{
		auto MutableWindow = ConstCastSharedPtr<FVoxelLandM5BaselineWindow>(Window);
		auto MutableChunks = ConstCastSharedPtr<FVoxelLandM5ScratchChunks>(Source);
		if (!MutableWindow || !MutableChunks || MutableChunks->Chunks.Num() != 1
			|| MutableChunks->Chunks[0].Density.Num() != 35 * 35 * 35 || MutableWindow->VoxelSizeCm != 100)
		{
			return false;
		}
		const FIntVector Size = MutableWindow->Size;
		const FVoxelLandM5PointCm Origin = MutableWindow->OriginCm;
		if ((900 - Origin.X) % 100 != 0 || (900 - Origin.Y) % 100 != 0 || (1900 - Origin.Z) % 100 != 0)
		{
			return false;
		}
		const int64 WX = (900 - Origin.X) / 100;
		const int64 WY = (900 - Origin.Y) / 100;
		const int64 WZ = (1900 - Origin.Z) / 100;
		if (WX < 5 || WY < 5 || WZ < 5 || WX + 5 >= Size.X || WY + 5 >= Size.Y || WZ + 5 >= Size.Z)
		{
			return false;
		}
		for (int64 Z = WZ - 5; Z <= WZ + 5; ++Z)
		{
			for (int64 Y = WY - 5; Y <= WY + 5; ++Y)
			{
				for (int64 X = WX - 5; X <= WX + 5; ++X)
				{
					const int64 Index = X + int64(Size.X) * (Y + int64(Size.Y) * Z);
					if (!MutableWindow->Density.IsValidIndex(int32(Index)) || MutableWindow->Density[int32(Index)] <= 0)
					{
						return false;
					}
				}
			}
		}
		const int32 WindowIndex = int32(WX + int64(Size.X) * (WY + int64(Size.Y) * WZ));
		const int32 ChunkIndex = 10 + 35 * (10 + 35 * 20);
		Previous = MutableWindow->Density[WindowIndex];
		if (MutableChunks->Chunks[0].Density[ChunkIndex] != Previous)
		{
			return false;
		}
		MutableWindow->Density[WindowIndex] = 0;
		MutableChunks->Chunks[0].Density[ChunkIndex] = 0;
		return true;
	}

	static void SetIsolatedChunkSample(
		const TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe>& Source, int16 Value)
	{
		ConstCastSharedPtr<FVoxelLandM5ScratchChunks>(Source)->Chunks[0].Density[10 + 35 * (10 + 35 * 20)] = Value;
	}

	static void RestoreIsolatedWindowSample(
		const TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe>& Window, int16 Value)
	{
		auto Mutable = ConstCastSharedPtr<FVoxelLandM5BaselineWindow>(Window);
		const FIntVector Size = Mutable->Size;
		const int64 X = (900 - Mutable->OriginCm.X) / 100;
		const int64 Y = (900 - Mutable->OriginCm.Y) / 100;
		const int64 Z = (1900 - Mutable->OriginCm.Z) / 100;
		Mutable->Density[int32(X + int64(Size.X) * (Y + int64(Size.Y) * Z))] = Value;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5ScratchRoofCheckTest, "VoxelLand.M5.ScratchRoof.Preserved",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5ScratchRoofCheckTest::RunTest(const FString& Parameters)
{
	bool bPreserved = false, bBlocked = false, bCancelled = false, bChangedSettings = false, bForeign = false;
	bool bZeroPreserved = false, bZeroBlocked = false, bExactZero = false;
	bool bIsolatedSetup = false, bIsolatedPreserved = false, bIsolatedBlocked = false;
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
				!= EVoxelLandM5ErrorCode::None
			|| !Candidate)
		{
			return false;
		}
		FVoxelLandM5StageManifest Manifest;
		TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Chunks;
		if (FVoxelLandM5ScratchChunks::Build(S, Manifest, 42, Window.ToSharedRef(), Candidate.ToSharedRef(), nullptr,
				FIntVector(0, 0, 0), FIntVector(1, 1, 1), 1, Budget.ToSharedRef(), Never, Chunks)
					.Code
				!= EVoxelLandM5ErrorCode::None
			|| !Chunks)
		{
			return false;
		}
		using namespace VoxelLand::M5::ScratchRoofCheck;
		bPreserved = Check(S, 42, Manifest, Window.ToSharedRef(), Candidate.ToSharedRef(), nullptr,
						 Chunks.ToSharedRef(), Budget.ToSharedRef(), Never)
						 .Code
				  == EVoxelLandM5ErrorCode::None;
		const int16 Original = FVoxelLandM5ScratchRoofFixtureFactory::ChangeSample(Chunks);
		bBlocked = Check(S, 42, Manifest, Window.ToSharedRef(), Candidate.ToSharedRef(), nullptr, Chunks.ToSharedRef(),
					   Budget.ToSharedRef(), Never)
					   .Code
				== EVoxelLandM5ErrorCode::Blocked;
		FVoxelLandM5ScratchRoofFixtureFactory::RestoreSample(Chunks, Original);
		bCancelled = Check(S, 42, Manifest, Window.ToSharedRef(), Candidate.ToSharedRef(), nullptr,
						 Chunks.ToSharedRef(), Budget.ToSharedRef(), [] { return true; })
						 .Code
				  == EVoxelLandM5ErrorCode::Cancelled;
		FVoxelLandM5CaveSettings Changed = S;
		Changed.RoofThicknessCm = 100;
		bChangedSettings = Check(Changed, 42, Manifest, Window.ToSharedRef(), Candidate.ToSharedRef(), nullptr,
							   Chunks.ToSharedRef(), Budget.ToSharedRef(), Never)
							   .Code
						== EVoxelLandM5ErrorCode::InvalidInput;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Other;
		if (!FVoxelLandM5Budget::TryCreate(256 * 1024 * 1024, 1000000000, Other))
		{
			return false;
		}
		bForeign = Check(S, 42, Manifest, Window.ToSharedRef(), Candidate.ToSharedRef(), nullptr, Chunks.ToSharedRef(),
					   Other.ToSharedRef(), Never)
					   .Code
				== EVoxelLandM5ErrorCode::InvalidInput;

		// Surface at Z=0 creates exact zero samples on the stored lattice.
		// Use separate admission so the earlier roof scans do not affect this case.
		Heights.Init(0, 129 * 129);
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> ZeroGrid;
		if (!FVoxelLandM4HeightGrid::Create(-5000, -5000, 128, 128, 100, Heights, ZeroGrid))
		{
			return false;
		}
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> ZeroGenerator;
		if (FVoxelLandGenerator::CreateM4HeightField(G, ZeroGrid, false, ZeroGenerator) || !ZeroGenerator)
		{
			return false;
		}
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> ZeroBudget;
		if (!FVoxelLandM5Budget::TryCreate(256 * 1024 * 1024, 1000000000, ZeroBudget))
		{
			return false;
		}
		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> ZeroWindow;
		if (FVoxelLandM5BaselineWindow::Build(ZeroGenerator.ToSharedRef(), FIntVector(-1, -1, -1), FIntVector(2, 2, 2),
				27, ZeroBudget.ToSharedRef(), Never, ZeroWindow)
					.Code
				!= EVoxelLandM5ErrorCode::None
			|| !ZeroWindow)
		{
			return false;
		}
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> ZeroCandidate;
		if (VoxelLand::M5::CandidatePlanner::Build(S, 42, {}, ZeroBudget.ToSharedRef(), Never, ZeroCandidate).Code
				!= EVoxelLandM5ErrorCode::None
			|| !ZeroCandidate)
		{
			return false;
		}
		TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> ZeroChunks;
		if (FVoxelLandM5ScratchChunks::Build(S, Manifest, 42, ZeroWindow.ToSharedRef(), ZeroCandidate.ToSharedRef(),
				nullptr, FIntVector(0, 0, 0), FIntVector(1, 1, 1), 1, ZeroBudget.ToSharedRef(), Never, ZeroChunks)
					.Code
				!= EVoxelLandM5ErrorCode::None
			|| !ZeroChunks)
		{
			return false;
		}
		bZeroPreserved = Check(S, 42, Manifest, ZeroWindow.ToSharedRef(), ZeroCandidate.ToSharedRef(), nullptr,
							 ZeroChunks.ToSharedRef(), ZeroBudget.ToSharedRef(), Never)
							 .Code
					  == EVoxelLandM5ErrorCode::None;
		const int16 ZeroOriginal = FVoxelLandM5ScratchRoofFixtureFactory::ChangeSample(ZeroChunks);
		bExactZero = ZeroOriginal == 0;
		bZeroBlocked = Check(S, 42, Manifest, ZeroWindow.ToSharedRef(), ZeroCandidate.ToSharedRef(), nullptr,
						   ZeroChunks.ToSharedRef(), ZeroBudget.ToSharedRef(), Never)
						   .Code
					== EVoxelLandM5ErrorCode::Blocked;
		FVoxelLandM5ScratchRoofFixtureFactory::RestoreSample(ZeroChunks, ZeroOriginal);

		// The checked neighborhood is strictly positive before installing the
		// isolated zero, so mixed-sign cells cannot cause this rejection.
		int16 IsolatedOriginal = 0;
		bIsolatedSetup =
			FVoxelLandM5ScratchRoofFixtureFactory::SetIsolatedZero(ZeroWindow, ZeroChunks, IsolatedOriginal);
		if (bIsolatedSetup)
		{
			bIsolatedPreserved = Check(S, 42, Manifest, ZeroWindow.ToSharedRef(), ZeroCandidate.ToSharedRef(), nullptr,
									 ZeroChunks.ToSharedRef(), ZeroBudget.ToSharedRef(), Never)
									 .Code
							  == EVoxelLandM5ErrorCode::None;
			FVoxelLandM5ScratchRoofFixtureFactory::SetIsolatedChunkSample(ZeroChunks, 1);
			bIsolatedBlocked = Check(S, 42, Manifest, ZeroWindow.ToSharedRef(), ZeroCandidate.ToSharedRef(), nullptr,
								   ZeroChunks.ToSharedRef(), ZeroBudget.ToSharedRef(), Never)
								   .Code
							== EVoxelLandM5ErrorCode::Blocked;
			FVoxelLandM5ScratchRoofFixtureFactory::SetIsolatedChunkSample(ZeroChunks, IsolatedOriginal);
			FVoxelLandM5ScratchRoofFixtureFactory::RestoreIsolatedWindowSample(ZeroWindow, IsolatedOriginal);
		}
		return true;
	}).Get();
	TestTrue(TEXT("fixture setup"), bSetup);
	TestTrue(TEXT("baseline roof samples remain unchanged"), bPreserved);
	TestTrue(TEXT("changed roof influence is blocked"), bBlocked);
	TestTrue(TEXT("cancellation fails"), bCancelled);
	TestTrue(TEXT("changed roof settings fail"), bChangedSettings);
	TestTrue(TEXT("foreign budget fails"), bForeign);
	TestTrue(TEXT("zero-height fixture has an exact zero sample"), bExactZero);
	TestTrue(TEXT("unchanged zero-crossing roof is preserved"), bZeroPreserved);
	TestTrue(TEXT("changed zero-crossing roof influence is blocked"), bZeroBlocked);
	TestTrue(TEXT("isolated zero is surrounded by positive baseline samples"), bIsolatedSetup);
	TestTrue(TEXT("unchanged isolated zero is preserved"), bIsolatedPreserved);
	TestTrue(TEXT("changed isolated zero influence is blocked"), bIsolatedBlocked);
	return bSetup;
}

#endif
