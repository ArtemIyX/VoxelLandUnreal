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
#include "Generation/Caves/Planning/VoxelLandCaveScratchRoofCheck.h"
#include "Hash/xxhash.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5ScratchChunksTest, "VoxelLand.M5.ScratchChunks.Finite",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5ScratchChunksTest::RunTest(const FString& Parameters)
{
	bool bSuccess = false, bHalos = false, bHashes = false, bBounds = false, bLimit = false;
	bool bCancelled = false, bForeign = false, bDependencies = false, bLifetime = false;
	bool bPocketChunks = false, bPocketSettings = false, bPocketRoof = false;
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
		Heights.Init(50 * 256, 161 * 129);
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
		if (!FVoxelLandM4HeightGrid::Create(-5000, -5000, 160, 128, 100, Heights, Grid))
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
		if (FVoxelLandM5BaselineWindow::Build(Generator.ToSharedRef(), FIntVector(-1, -1, -1), FIntVector(3, 2, 2), 36,
				Budget.ToSharedRef(), Never, Window)
					.Code
				!= EVoxelLandM5ErrorCode::None
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
		if (VoxelLand::M5::CandidatePlanner::Build(Settings, 42, {}, Budget.ToSharedRef(), Never, Candidate).Code
				!= EVoxelLandM5ErrorCode::None
			|| !Candidate)
		{
			return false;
		}
		const FVoxelLandM5StageManifest Manifest;
		const FIntVector WindowSize = Window->GetSize();
		const FIntVector MaskSize(WindowSize.X + 1, WindowSize.Y + 1, WindowSize.Z + 1);
		FVoxelLandM5PointCm MaskOrigin = Window->GetOriginCm();
		MaskOrigin.X -= 100;
		MaskOrigin.Y -= 100;
		MaskOrigin.Z -= 100;
		TArray<uint8> Cells;
		Cells.Init(0, MaskSize.X * MaskSize.Y * MaskSize.Z);
		TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> Protection;
		if (FVoxelLandM5ProtectionGrid::Capture(
				MaskSize, MaskOrigin, 100, Cells, Budget.ToSharedRef(), Never, Protection)
					.Code
				!= EVoxelLandM5ErrorCode::None
			|| !Protection)
		{
			return false;
		}
		TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Out;
		FVoxelLandM5Diagnostic D =
			FVoxelLandM5ScratchChunks::Build(Settings, Manifest, 42, Window.ToSharedRef(), Candidate.ToSharedRef(),
				Protection, FIntVector(0, 0, 0), FIntVector(2, 1, 1), 2, Budget.ToSharedRef(), Never, Out);
		bSuccess = D.Code == EVoxelLandM5ErrorCode::None && Out && Out->GetChunks().Num() == 2;
		if (!bSuccess)
		{
			return true;
		}
		const TConstArrayView<FVoxelLandChunkData> Chunks = Out->GetChunks();
		bSuccess = Chunks[0].Key.Coordinate == FIntVector(0, 0, 0) && Chunks[1].Key.Coordinate == FIntVector(1, 0, 0)
				&& Chunks[0].Key.LOD == 0 && Chunks[1].Key.LOD == 0 && Out->GetMinInclusive() == FIntVector(0, 0, 0)
				&& Out->GetMaxExclusive() == FIntVector(2, 1, 1);
		bHalos = true;
		for (int32 Z = 0; Z < FVoxelLandChunkData::SamplesPerAxis; ++Z)
		{
			for (int32 Y = 0; Y < FVoxelLandChunkData::SamplesPerAxis; ++Y)
			{
				for (int32 X = 0; X < 3; ++X)
				{
					const int32 Row =
						FVoxelLandChunkData::SamplesPerAxis * (Y + FVoxelLandChunkData::SamplesPerAxis * Z);
					bHalos &= Chunks[0].Density[Row + FVoxelLandChunkData::Cells + X] == Chunks[1].Density[Row + X];
				}
			}
		}
		bHashes = true;
		for (const FVoxelLandChunkData& Chunk : Chunks)
		{
			TArray<uint8> Bytes;
			Bytes.Reserve(Chunk.Density.Num() * 2);
			for (int16 Value : Chunk.Density)
			{
				const uint16 Bits = uint16(Value);
				Bytes.Add(uint8(Bits));
				Bytes.Add(uint8(Bits >> 8));
			}
			bHashes &= Chunk.Hash == FXxHash64::HashBuffer(Bytes.GetData(), Bytes.Num()).Hash;
		}
		TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Original = Out;
		D = FVoxelLandM5ScratchChunks::Build(Settings, Manifest, 42, Window.ToSharedRef(), Candidate.ToSharedRef(),
			Protection, FIntVector(0, 0, 0), FIntVector(3, 1, 1), 3, Budget.ToSharedRef(), Never, Out);
		bBounds = D.Code == EVoxelLandM5ErrorCode::OutOfDomain && Out == Original;
		D = FVoxelLandM5ScratchChunks::Build(Settings, Manifest, 42, Window.ToSharedRef(), Candidate.ToSharedRef(),
			Protection, FIntVector(0, 0, 0), FIntVector(2, 1, 1), 1, Budget.ToSharedRef(), Never, Out);
		bLimit = D.Code == EVoxelLandM5ErrorCode::OutOfDomain && Out == Original;
		D = FVoxelLandM5ScratchChunks::Build(
			Settings, Manifest, 42, Window.ToSharedRef(), Candidate.ToSharedRef(), Protection, FIntVector(0, 0, 0),
			FIntVector(2, 1, 1), 2, Budget.ToSharedRef(), [] { return true; }, Out);
		bCancelled = D.Code == EVoxelLandM5ErrorCode::Cancelled && Out == Original;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Other;
		if (!FVoxelLandM5Budget::TryCreate(256 * 1024 * 1024, 1000000000, Other))
		{
			return false;
		}
		D = FVoxelLandM5ScratchChunks::Build(Settings, Manifest, 42, Window.ToSharedRef(), Candidate.ToSharedRef(),
			Protection, FIntVector(0, 0, 0), FIntVector(2, 1, 1), 2, Other.ToSharedRef(), Never, Out);
		bForeign = D.Code == EVoxelLandM5ErrorCode::InvalidInput && Out == Original;

		TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> OtherWindow;
		if (FVoxelLandM5BaselineWindow::Build(Generator.ToSharedRef(), FIntVector(0, 0, 0), FIntVector(1, 1, 1), 1,
				Budget.ToSharedRef(), Never, OtherWindow)
					.Code
				!= EVoxelLandM5ErrorCode::None
			|| !OtherWindow)
		{
			return false;
		}
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> OtherCandidate;
		if (VoxelLand::M5::CandidatePlanner::Build(Settings, 42, {}, Budget.ToSharedRef(), Never, OtherCandidate).Code
				!= EVoxelLandM5ErrorCode::None
			|| !OtherCandidate)
		{
			return false;
		}
		TArray<uint8> OtherCells;
		OtherCells.Init(0, 1);
		TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> OtherProtection;
		if (FVoxelLandM5ProtectionGrid::Capture(
				FIntVector(1), FVoxelLandM5PointCm{}, 100, OtherCells, Budget.ToSharedRef(), Never, OtherProtection)
					.Code
				!= EVoxelLandM5ErrorCode::None
			|| !OtherProtection)
		{
			return false;
		}
		bDependencies = Out->MatchesDependencies(*Window, *Candidate, Protection.Get(), 42, Manifest)
					 && !Out->MatchesDependencies(*OtherWindow, *Candidate, Protection.Get(), 42, Manifest)
					 && !Out->MatchesDependencies(*Window, *OtherCandidate, Protection.Get(), 42, Manifest)
					 && !Out->MatchesDependencies(*Window, *Candidate, OtherProtection.Get(), 42, Manifest)
					 && !Out->MatchesDependencies(*Window, *Candidate, nullptr, 42, Manifest)
					 && !Out->MatchesDependencies(*Window, *Candidate, Protection.Get(), 43, Manifest);
		int32 FVoxelLandM5StageManifest::* Versions[] = {&FVoxelLandM5StageManifest::Descriptor,
			&FVoxelLandM5StageManifest::Seed, &FVoxelLandM5StageManifest::Topology,
			&FVoxelLandM5StageManifest::Geometry, &FVoxelLandM5StageManifest::RegionalOwnership,
			&FVoxelLandM5StageManifest::PocketNoise, &FVoxelLandM5StageManifest::SurfaceValidation,
			&FVoxelLandM5StageManifest::Composition};
		for (auto Version : Versions)
		{
			FVoxelLandM5StageManifest Changed = Manifest;
			++(Changed.*Version);
			bDependencies &= !Out->MatchesDependencies(*Window, *Candidate, Protection.Get(), 42, Changed);
		}
		// Pocket-only stages carry no invented network candidate or openings.
		FVoxelLandM5CaveSettings Pocket = Settings;
		Pocket.bEnabled = false;
		Pocket.Topology.EntranceCount = 1; // Inactive network topology must not create openings.
		Pocket.Pockets.bEnabled = true;
		Pocket.Pockets.AmplitudeCm = 0;
		Pocket.Pockets.MinDepthCm = 0;
		Pocket.Pockets.MaxDepthCm = 5000;
		TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> PocketChunks;
		D = FVoxelLandM5ScratchChunks::Build(Pocket, Manifest, 42, Window.ToSharedRef(), nullptr, Protection,
			FIntVector(0, 0, 0), FIntVector(1, 1, 1), 1, Budget.ToSharedRef(), Never, PocketChunks);
		bPocketChunks = D.Code == EVoxelLandM5ErrorCode::None && PocketChunks && PocketChunks->GetChunks().Num() == 1
					 && PocketChunks->MatchesDependencies(*Window,
						 static_cast<const FVoxelLandM5CandidateResult*>(nullptr), Protection.Get(), 42, Manifest);
		if (bPocketChunks)
		{
			FVoxelLandM5CaveSettings ChangedPocket = Pocket;
			++ChangedPocket.Pockets.Salt;
			bPocketSettings = PocketChunks->MatchesSettings(Pocket) && !PocketChunks->MatchesSettings(ChangedPocket);
			bPocketRoof = VoxelLand::M5::ScratchRoofCheck::Check(Pocket, 42, Manifest, Window.ToSharedRef(), nullptr,
							  Protection, PocketChunks.ToSharedRef(), Budget.ToSharedRef(), Never)
							  .Code
					   == EVoxelLandM5ErrorCode::None;
		}
		PocketChunks.Reset();
		TWeakPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> WeakWindow = Window;
		TWeakPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> WeakCandidate = Candidate;
		TWeakPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> WeakProtection = Protection;
		Window.Reset();
		Candidate.Reset();
		Protection.Reset();
		bLifetime = WeakWindow.IsValid() && WeakCandidate.IsValid() && WeakProtection.IsValid();
		Original.Reset();
		Out.Reset();
		bLifetime &= !WeakWindow.IsValid() && !WeakCandidate.IsValid() && !WeakProtection.IsValid();
		return true;
	}).Get();
	TestTrue(TEXT("fixture setup"), bSetup);
	TestTrue(TEXT("finite chunk rectangle"), bSuccess);
	TestTrue(TEXT("shared chunk halos agree"), bHalos);
	TestTrue(TEXT("canonical little-endian density hashes"), bHashes);
	TestTrue(TEXT("missing outer halo fails without replacing output"), bBounds);
	TestTrue(TEXT("chunk limit fails without replacing output"), bLimit);
	TestTrue(TEXT("cancellation preserves output"), bCancelled);
	TestTrue(TEXT("foreign budget preserves output"), bForeign);
	TestTrue(TEXT("dependency identities, seed and every version match"), bDependencies);
	TestTrue(TEXT("scratch chunks retain and release their immediate inputs"), bLifetime);
	TestTrue(TEXT("pocket-only chunks have no network candidate"), bPocketChunks);
	TestTrue(TEXT("pocket-only settings are bound exactly"), bPocketSettings);
	TestTrue(TEXT("pocket-only roof check has no network openings"), bPocketRoof);
	return bSetup;
}

#endif
