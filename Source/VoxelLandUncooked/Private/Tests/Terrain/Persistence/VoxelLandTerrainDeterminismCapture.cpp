#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightGrid.h"
#include "Generation/Terrain/Controls/VoxelLandControlGrid.h"
#include "Generation/Terrain/Curves/VoxelLandCurveLut.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightPrepass.h"
#include "../../Field/VoxelLandFieldEvaluationFixtures.h"
#include "Generation/Terrain/Heightfield/VoxelLandTerrainPrepass.h"
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

struct FM4ControlCaptureResult
{
	FString Error;
	TArray<uint16> Controls;
	TArray<uint16> Repeat;
	TArray<int32> CurveOutputs;
	TArray<int32> Heights;
	bool bReferenceMatches = false;
};

FM4ControlCaptureResult M4CaptureControlStages(int32 Seed, const FVoxelLandM4DomainSettings& Domain)
{
	FM4ControlCaptureResult Result;
	using VoxelLandM1Fixtures::Op;
	FVoxelLandProgramDraft Draft;
	Draft.RegisterCount = 3;
	Draft.Constants = {0.002f, 3.f, 0.5f, 2.f};
	FVoxelLandSeedSlot SeedSlot;
	SeedSlot.NodeId = FGuid(0x12345678, 0x23456789, 0x34567890, 0x45678901);
	SeedSlot.Salt = 17;
	Draft.Seeds.Add(SeedSlot);
	FVoxelLandNoiseConfig Noise;
	Noise.SeedSlot = 0;
	Noise.Frequency.Index = 0;
	Noise.Octaves.Index = 1;
	Noise.Gain.Index = 2;
	Noise.Lacunarity.Index = 3;
	Draft.Noise.Add(Noise);
	Draft.Instructions = {Op(EVoxelLandOpcode::LoadX, 0), Op(EVoxelLandOpcode::LoadY, 1),
		Op(EVoxelLandOpcode::Perlin2, 2, 0, 1, 0, 0), Op(EVoxelLandOpcode::StoreDensity, 0, 2)};
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
	if (const FVoxelLandError Error = FVoxelLandProgram::Validate(Draft, Program))
	{
		Result.Error = Error.Message;
		return Result;
	}
	FVoxelLandGeneratorSettings Settings;
	Settings.Program = Program;
	Settings.WorldSeed = Seed;
	Settings.Backends.Noise = EVoxelLandNoiseBackend::SSE2;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
	if (const FVoxelLandError Error = FVoxelLandGenerator::Create(Settings, Generator))
	{
		Result.Error = Error.Message;
		return Result;
	}
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	if (const FVoxelLandError Error = Generator->CreateWorker(Worker))
	{
		Result.Error = Error.Message;
		return Result;
	}
	const auto Evaluate = [&](TArray<uint16>& Output) {
		return VoxelLandM4ControlGrid::Evaluate(*Generator, *Worker, Domain.OriginXCm, Domain.OriginYCm,
			Domain.GridSpacingCm, Domain.CellsX, Domain.CellsY, Output, [] { return false; });
	};
	if (const FVoxelLandError Error = Evaluate(Result.Controls))
	{
		Result.Error = Error.Message;
		return Result;
	}
	if (const FVoxelLandError Error = Evaluate(Result.Repeat))
	{
		Result.Error = Error.Message;
		return Result;
	}
	TArray<int32> Samples;
	for (int32 I = 0; I <= 4096; ++I)
	{
		Samples.Add(I - 2048);
	}
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
	if (!FVoxelLandM4CurveLut::Create(Samples, -2048, 2048, Curve, Result.Error))
	{
		return Result;
	}
	if (const FVoxelLandError Error = VoxelLandM4HeightPrepass::BuildBaseHeights(Domain, Result.Controls, *Curve,
			Result.Controls, *Curve, Result.Controls, *Curve, 12345, Result.Heights, [] { return false; }))
	{
		Result.Error = Error.Message;
		return Result;
	}
	if (Result.Heights.Num() != Result.Controls.Num())
	{
		Result.Error = TEXT("Control/height count mismatch");
		return Result;
	}
	Result.bReferenceMatches = true;
	for (int32 I = 0; I < Result.Controls.Num(); ++I)
	{
		const int64 Numerator = int64(Result.Controls[I]) * 4096 - int64(2048) * 65535;
		const int32 Expected = int32(Numerator >= 0 ? (Numerator + 32767) / 65535 : -((-Numerator + 32767) / 65535));
		const int32 Actual = Curve->Evaluate(Result.Controls[I]);
		Result.CurveOutputs.Add(Actual);
		Result.bReferenceMatches &= Actual == Expected && Result.Heights[I] == 12345 + 3 * Expected;
	}
	return Result;
}

uint64 M4DeterminismControlHash(const TArray<uint16>& Values)
{
	uint64 Hash = 14695981039346656037ull;
	for (uint16 Value : Values)
	{
		Hash = (Hash ^ uint8(Value)) * 1099511628211ull;
		Hash = (Hash ^ uint8(Value >> 8)) * 1099511628211ull;
	}
	return Hash;
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
	const FVoxelLandError ProgramError = Async(EAsyncExecution::ThreadPool, [&Draft, &Program] {
		return FVoxelLandProgram::Validate(Draft, Program);
	}).Get();
	if (ProgramError)
	{
		AddError(ProgramError.Message);
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
	Lines.Add(TEXT("fixture=VoxelLand.M4.Determinism.Capture.v2"));
	Lines.Add(TEXT("control=perlin2-and-linear-lut;erosion=synthetic-q8;noise=native-sse2;workers=independent"));
	Lines.Add(FString::Printf(TEXT("input_fnv1a64=%016llx"), M4DeterminismInputHash(Input)));
	const int32 Seeds[] = {1, 271828, -12345};
	for (const int32 Seed : Seeds)
	{
		const FM4ControlCaptureResult Control =
			Async(EAsyncExecution::ThreadPool, [Seed, Domain] { return M4CaptureControlStages(Seed, Domain); }).Get();
		if (!Control.Error.IsEmpty())
		{
			AddError(FString::Printf(TEXT("Seed %d control stage: %s"), Seed, *Control.Error));
			return false;
		}
		if (!TestEqual(TEXT("control sample count"), Control.Controls.Num(), 81)
			|| !TestTrue(TEXT("repeated control evaluation matches"), Control.Controls == Control.Repeat)
			|| !TestTrue(
				TEXT("linear curve and height match independent integer reference"), Control.bReferenceMatches))
		{
			return false;
		}
		bool bVaried = false;
		for (uint16 Value : Control.Controls)
		{
			bVaried |= Value != Control.Controls[0];
		}
		if (!TestTrue(TEXT("control noise is nonconstant"), bVaried))
		{
			return false;
		}
		Lines.Add(FString::Printf(TEXT("seed=%d control_fnv1a64=%016llx curve_fnv1a64=%016llx height_fnv1a64=%016llx"),
			Seed, M4DeterminismControlHash(Control.Controls), M4DeterminismInputHash(Control.CurveOutputs),
			M4DeterminismInputHash(Control.Heights)));
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
		uint64 GridHash = 0;
		const FVoxelLandError PrepassError = Async(EAsyncExecution::ThreadPool, [&] {
			return VoxelLandM4Prepass::Generate(
				Domain, Input, Seed, FGuid(1, 2, 3, 4), 7, 16, Erosion, Grid, GridHash, [] { return false; });
		}).Get();
		if (PrepassError)
		{
			AddError(FString::Printf(TEXT("Seed %d prepass: %s"), Seed, *PrepassError.Message));
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
		const FVoxelLandError GeneratorError = Async(EAsyncExecution::ThreadPool, [&] {
			return FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, true, Overhang, Generator);
		}).Get();
		if (GeneratorError)
		{
			AddError(FString::Printf(TEXT("Seed %d generator: %s"), Seed, *GeneratorError.Message));
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
