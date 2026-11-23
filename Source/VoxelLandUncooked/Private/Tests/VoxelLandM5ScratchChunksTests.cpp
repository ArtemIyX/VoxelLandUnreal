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
#include "Hash/xxhash.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5ScratchChunksTest, "VoxelLand.M5.ScratchChunks.Finite",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5ScratchChunksTest::RunTest(const FString& Parameters)
{
	bool bSuccess = false, bHalos = false, bHashes = false, bBounds = false, bLimit = false;
	bool bCancelled = false, bForeign = false;
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
		if (FVoxelLandM5BaselineWindow::Build(Generator.ToSharedRef(), FIntVector(-1, -1, -1),
				FIntVector(3, 2, 2), 36, Budget.ToSharedRef(), Never, Window).Code != EVoxelLandM5ErrorCode::None
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
				!= EVoxelLandM5ErrorCode::None || !Candidate)
		{
			return false;
		}
		const FVoxelLandM5StageManifest Manifest;
		TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Out;
		FVoxelLandM5Diagnostic D = FVoxelLandM5ScratchChunks::Build(Settings, Manifest, 42, *Window,
			Candidate.ToSharedRef(), nullptr, FIntVector(0, 0, 0), FIntVector(2, 1, 1),
			2, Budget.ToSharedRef(), Never, Out);
		bSuccess = D.Code == EVoxelLandM5ErrorCode::None && Out && Out->GetChunks().Num() == 2;
		if (!bSuccess)
		{
			return true;
		}
		const TConstArrayView<FVoxelLandChunkData> Chunks = Out->GetChunks();
		bSuccess = Chunks[0].Key.Coordinate == FIntVector(0, 0, 0) && Chunks[1].Key.Coordinate == FIntVector(1, 0, 0)
			&& Chunks[0].Key.LOD == 0 && Chunks[1].Key.LOD == 0
			&& Out->GetMinInclusive() == FIntVector(0, 0, 0) && Out->GetMaxExclusive() == FIntVector(2, 1, 1);
		bHalos = true;
		for (int32 Z = 0; Z < FVoxelLandChunkData::SamplesPerAxis; ++Z)
		{
			for (int32 Y = 0; Y < FVoxelLandChunkData::SamplesPerAxis; ++Y)
			{
				for (int32 X = 0; X < 3; ++X)
				{
					const int32 Row = FVoxelLandChunkData::SamplesPerAxis * (Y + FVoxelLandChunkData::SamplesPerAxis * Z);
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
		const TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Original = Out;
		D = FVoxelLandM5ScratchChunks::Build(Settings, Manifest, 42, *Window, Candidate.ToSharedRef(), nullptr,
			FIntVector(0, 0, 0), FIntVector(3, 1, 1), 3, Budget.ToSharedRef(), Never, Out);
		bBounds = D.Code == EVoxelLandM5ErrorCode::OutOfDomain && Out == Original;
		D = FVoxelLandM5ScratchChunks::Build(Settings, Manifest, 42, *Window, Candidate.ToSharedRef(), nullptr,
			FIntVector(0, 0, 0), FIntVector(2, 1, 1), 1, Budget.ToSharedRef(), Never, Out);
		bLimit = D.Code == EVoxelLandM5ErrorCode::OutOfDomain && Out == Original;
		D = FVoxelLandM5ScratchChunks::Build(Settings, Manifest, 42, *Window, Candidate.ToSharedRef(), nullptr,
			FIntVector(0, 0, 0), FIntVector(2, 1, 1), 2, Budget.ToSharedRef(), [] { return true; }, Out);
		bCancelled = D.Code == EVoxelLandM5ErrorCode::Cancelled && Out == Original;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Other;
		if (!FVoxelLandM5Budget::TryCreate(256 * 1024 * 1024, 1000000000, Other))
		{
			return false;
		}
		D = FVoxelLandM5ScratchChunks::Build(Settings, Manifest, 42, *Window, Candidate.ToSharedRef(), nullptr,
			FIntVector(0, 0, 0), FIntVector(2, 1, 1), 2, Other.ToSharedRef(), Never, Out);
		bForeign = D.Code == EVoxelLandM5ErrorCode::InvalidInput && Out == Original;
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
	return bSetup;
}

#endif
