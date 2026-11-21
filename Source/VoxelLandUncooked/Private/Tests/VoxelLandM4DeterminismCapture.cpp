#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Generation/VoxelLandM4Prepass.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"

namespace
{
constexpr EAutomationTestFlags M4DeterminismFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

struct FM4DeterminismChunkResult
{
	FVoxelLandError Error;
	uint64 Hash = 0;
	TArray<int16> Density;
};

FM4DeterminismChunkResult M4DeterminismGenerate(
	const TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& Generator, int32 X)
{
	FM4DeterminismChunkResult Result;
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	Result.Error = Generator->CreateWorker(Worker);
	if (Result.Error)
	{
		return Result;
	}
	FVoxelLandChunkKey Key;
	Key.Coordinate = FIntVector(X, 0, 0);
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk;
	Result.Error = Generator->GenerateChunk(Key, *Worker, Chunk);
	if (!Result.Error)
	{
		Result.Hash = Chunk->Hash;
		Result.Density = Chunk->Density;
	}
	return Result;
}

// FNV-1a of explicitly little-endian Q8 input samples, independent of host byte order.
uint64 M4DeterminismInputHash(const TArray<int32>& Values)
{
	uint64 Hash = 14695981039346656037ull;
	for (int32 Value : Values)
	{
		const uint32 Bits = uint32(Value);
		for (int32 Shift = 0; Shift < 32; Shift += 8)
		{
			Hash = (Hash ^ uint8(Bits >> Shift)) * 1099511628211ull;
		}
	}
	return Hash;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4DeterminismCapture, "VoxelLand.M4.Determinism.Capture", M4DeterminismFlags)
bool FVoxelLandM4DeterminismCapture::RunTest(const FString& Parameters)
{
	FVoxelLandProgramDraft Draft;
	Draft.FormatVersion = 2;
	Draft.RegisterCount = 1;
	FVoxelLandInstruction Load;
	Load.Op = EVoxelLandOpcode::LoadZ;
	FVoxelLandInstruction Store;
	Store.Op = EVoxelLandOpcode::StoreDensity;
	Draft.Instructions = {Load, Store};
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
	if (const FVoxelLandError Error = FVoxelLandProgram::Validate(Draft, Program))
	{
		AddError(Error.Message);
		return false;
	}

	FVoxelLandM4DomainSettings Domain;
	Domain.CellsX = Domain.CellsY = 8;
	Domain.OriginXCm = Domain.OriginYCm = -6400;
	Domain.GridSpacingCm = 1600;
	Domain.bGlobalErosion = true;
	FVoxelLandM4ErosionSettings Erosion;
	Erosion.ThermalIterations = 1;
	TArray<int32> Input;
	for (int32 Y = 0; Y <= 8; ++Y)
	{
		for (int32 X = 0; X <= 8; ++X)
		{
			Input.Add((X * X * 71 - Y * 193 + ((X * 17 + Y * 11) % 7) * 331) * 256);
		}
	}
	TArray<FString> Lines;
	Lines.Add(TEXT("fixture=VoxelLand.M4.Determinism.Capture.v1"));
	Lines.Add(TEXT("input=synthetic-q8;noise=native-sse2;workers=independent"));
	Lines.Add(FString::Printf(TEXT("input_fnv1a64=%016llx"), M4DeterminismInputHash(Input)));
	const int32 Seeds[] = {1, 271828, -12345};
	for (const int32 Seed : Seeds)
	{
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
		uint64 GridHash = 0;
		if (const FVoxelLandError Error = VoxelLandM4Prepass::Generate(
				Domain, Input, Seed, FGuid(1, 2, 3, 4), 7, 16, Erosion, Grid, GridHash, [] { return false; }))
		{
			AddError(FString::Printf(TEXT("Seed %d prepass: %s"), Seed, *Error.Message));
			return false;
		}
		FVoxelLandGeneratorSettings Settings;
		Settings.Program = Program;
		Settings.Profile.VoxelSizeCm = 100.f;
		Settings.WorldSeed = Seed;
		Settings.Backends.Noise = EVoxelLandNoiseBackend::SSE2;
		FVoxelLandM4OverhangConfig Overhang;
		Overhang.Displacement.bEnabled = true;
		Overhang.Displacement.AmplitudeQ8 = 128000;
		Overhang.Displacement.BandHalfWidthQ8 = 256000;
		Overhang.FrequencyPerCm = 0.004f;
		Overhang.NodeId = FGuid(11, 22, 33, 44);
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
		if (const FVoxelLandError Error =
				FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, true, Overhang, Generator))
		{
			AddError(FString::Printf(TEXT("Seed %d generator: %s"), Seed, *Error.Message));
			return false;
		}
		// Both serial and parallel requests create and use workers on their own threads.
		const FM4DeterminismChunkResult A =
			Async(EAsyncExecution::ThreadPool, [Generator] { return M4DeterminismGenerate(Generator, -1); }).Get();
		const FM4DeterminismChunkResult B =
			Async(EAsyncExecution::ThreadPool, [Generator] { return M4DeterminismGenerate(Generator, 0); }).Get();
		TFuture<FM4DeterminismChunkResult> ReverseB =
			Async(EAsyncExecution::ThreadPool, [Generator] { return M4DeterminismGenerate(Generator, 0); });
		TFuture<FM4DeterminismChunkResult> ReverseA =
			Async(EAsyncExecution::ThreadPool, [Generator] { return M4DeterminismGenerate(Generator, -1); });
		const FM4DeterminismChunkResult ParallelB = ReverseB.Get();
		const FM4DeterminismChunkResult ParallelA = ReverseA.Get();
		if (A.Error || B.Error || ParallelA.Error || ParallelB.Error)
		{
			AddError(FString::Printf(TEXT("Seed %d chunk generation failed; native SSE2 must be available"), Seed));
			return false;
		}
		if (!TestEqual(TEXT("chunk -1 serial and reverse parallel"), A.Hash, ParallelA.Hash)
			|| !TestEqual(TEXT("chunk 0 serial and reverse parallel"), B.Hash, ParallelB.Hash)
			|| !TestTrue(TEXT("chunk -1 density bytes match"), A.Density == ParallelA.Density)
			|| !TestTrue(TEXT("chunk 0 density bytes match"), B.Density == ParallelB.Density))
		{
			return false;
		}
		Lines.Add(FString::Printf(
			TEXT("seed=%d grid=%016llx chunk_-1_0_0=%016llx chunk_0_0_0=%016llx"), Seed, GridHash, A.Hash, B.Hash));
	}
	FString CapturePath;
	if (FParse::Value(FCommandLine::Get(), TEXT("VoxelLandM4Capture="), CapturePath))
	{
		if (CapturePath.IsEmpty()
			|| !FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")) + TEXT("\n"), *CapturePath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_NoReplaceExisting))
		{
			AddError(FString::Printf(
				TEXT("Capture could not be created (existing files are never replaced): %s"), *CapturePath));
			return false;
		}
		AddInfo(FString::Printf(TEXT("M4 determinism capture created: %s"), *CapturePath));
	}
	return true;
}
#endif
