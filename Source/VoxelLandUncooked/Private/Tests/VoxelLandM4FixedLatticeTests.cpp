#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Data/Structs/VoxelLandM4UnlimitedConfig.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4CurveLut.h"
#include "Generation/VoxelLandM4FixedLattice.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M4FixedFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandInstruction M4FixedOp(EVoxelLandOpcode Op, uint16 Dst = 0, uint16 A = 0, uint16 B = 0, uint32 Imm = 0)
{
	FVoxelLandInstruction I;
	I.Op = Op;
	I.Dst = Dst;
	I.A = A;
	I.B = B;
	I.Imm = Imm;
	return I;
}

bool M4FixedOverlapX(const FVoxelLandChunkData& A, const FVoxelLandChunkData& B)
{
	for (int32 Z = 0; Z < 35; ++Z)
	{
		for (int32 Y = 0; Y < 35; ++Y)
		{
			for (int32 X = 0; X < 3; ++X)
			{
				if (A.Density[X + 32 + 35 * (Y + 35 * Z)] != B.Density[X + 35 * (Y + 35 * Z)])
				{
					return false;
				}
			}
		}
	}
	return true;
}

bool M4FixedOverlapY(const FVoxelLandChunkData& A, const FVoxelLandChunkData& B)
{
	for (int32 Z = 0; Z < 35; ++Z)
	{
		for (int32 Y = 0; Y < 3; ++Y)
		{
			for (int32 X = 0; X < 35; ++X)
			{
				if (A.Density[X + 35 * (Y + 32 + 35 * Z)] != B.Density[X + 35 * (Y + 35 * Z)])
				{
					return false;
				}
			}
		}
	}
	return true;
}

bool M4FixedOverlapZ(const FVoxelLandChunkData& A, const FVoxelLandChunkData& B)
{
	for (int32 Z = 0; Z < 3; ++Z)
	{
		for (int32 Y = 0; Y < 35; ++Y)
		{
			for (int32 X = 0; X < 35; ++X)
			{
				if (A.Density[X + 35 * (Y + 35 * (Z + 32))] != B.Density[X + 35 * (Y + 35 * Z)])
				{
					return false;
				}
			}
		}
	}
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4FixedLatticeTest, "VoxelLand.M4.FixedLattice.Seams", M4FixedFlags)
bool FVoxelLandM4FixedLatticeTest::RunTest(const FString& Parameters)
{
	FString Failure;
	bool bLegacyRejected = false, bPositiveSeam = false, bNegativeSeam = false, bRepeat = false, bUnsupported = false;
	bool bCancelPreserved = false, bYSeam = false, bZSeam = false;
	const bool bSuccess = Async(EAsyncExecution::ThreadPool, [&] {
		FVoxelLandProgramDraft Draft;
		Draft.FormatVersion = 2;
		Draft.RegisterCount = 3;
		Draft.Constants = {0.00002f, 3.f, 0.5f, 2.f};
		FVoxelLandSeedSlot Seed;
		Seed.NodeId = FGuid(1, 2, 3, 4);
		Seed.Salt = 17;
		Draft.Seeds.Add(Seed);
		FVoxelLandNoiseConfig Noise;
		Noise.SeedSlot = 0;
		Noise.Frequency.Index = 0;
		Noise.Octaves.Index = 1;
		Noise.Gain.Index = 2;
		Noise.Lacunarity.Index = 3;
		Draft.Noise.Add(Noise);
		Draft.Instructions = {M4FixedOp(EVoxelLandOpcode::LoadX, 0), M4FixedOp(EVoxelLandOpcode::LoadY, 1),
			M4FixedOp(EVoxelLandOpcode::Fbm2, 2, 0, 1), M4FixedOp(EVoxelLandOpcode::StoreDensity, 0, 2)};
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (const FVoxelLandError E = FVoxelLandProgram::Validate(Draft, Program))
		{
			Failure = E.Message;
			return false;
		}
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Controls[3];
		FVoxelLandGeneratorSettings Settings;
		Settings.Program = Program;
		Settings.Profile.VoxelSizeCm = 100.f;
		for (int32 I = 0; I < 3; ++I)
		{
			Settings.WorldSeed = 271828 + I;
			if (const FVoxelLandError E = FVoxelLandGenerator::Create(Settings, Controls[I]))
			{
				Failure = E.Message;
				return false;
			}
		}
		TArray<int32> LUT;
		for (int32 I = 0; I <= 4096; ++I)
		{
			LUT.Add((I - 2048) * 10);
		}
		TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
		if (!FVoxelLandM4CurveLut::Create(LUT, -20480, 20480, Curve, Failure))
		{
			return false;
		}
		FVoxelLandM4UnlimitedConfig Config;
		Config.ContinentalGenerator = Controls[0];
		Config.MountainGenerator = Controls[1];
		Config.PeaksGenerator = Controls[2];
		Config.ContinentalCurve = Config.MountainCurve = Config.PeaksCurve = Curve;
		Config.bNormalizeSlope = true;
		FVoxelLandM4SmoothSettings Smooth;
		Smooth.bEnabled = true;
		Smooth.Strength = 0.5f;
		Config.SmoothOverride = Smooth;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Legacy, Fixed;
		if (const FVoxelLandError E = FVoxelLandGenerator::CreateM4Unlimited(Settings, Config, Legacy))
		{
			Failure = E.Message;
			return false;
		}
		Config.Backend = EVoxelLandM4UnlimitedBackend::FixedLattice;
		if (const FVoxelLandError E = FVoxelLandGenerator::CreateM4Unlimited(Settings, Config, Fixed))
		{
			Failure = E.Message;
			return false;
		}
		FVoxelLandChunkKey Positive;
		Positive.Coordinate.X = 5243;
		bLegacyRejected = bool(Legacy->ValidateM4ChunkDemand(Positive)) && !Fixed->ValidateM4ChunkDemand(Positive);
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		if (const FVoxelLandError E = Fixed->CreateWorker(Worker))
		{
			Failure = E.Message;
			return false;
		}
		auto Generate = [&](int32 X, TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& Chunk) {
			FVoxelLandChunkKey Key;
			Key.Coordinate.X = X;
			const FVoxelLandError E = Fixed->GenerateChunk(Key, *Worker, Chunk);
			if (E)
			{
				Failure = E.Message;
				return false;
			}
			return true;
		};
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> A, B, C, D, Repeat;
		if (!Generate(5242, A) || !Generate(5243, B) || !Generate(-5244, C) || !Generate(-5243, D)
			|| !Generate(5242, Repeat))
		{
			return false;
		}
		bPositiveSeam = M4FixedOverlapX(*A, *B);
		bNegativeSeam = M4FixedOverlapX(*C, *D);
		bRepeat = A->Hash == Repeat->Hash && A->Density == Repeat->Density;
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> YA, YB, ZA, ZB;
		auto GenerateKey = [&](FIntVector Coordinate,
							   TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& Chunk) {
			FVoxelLandChunkKey Key;
			Key.Coordinate = Coordinate;
			const FVoxelLandError E = Fixed->GenerateChunk(Key, *Worker, Chunk);
			if (E)
			{
				Failure = E.Message;
				return false;
			}
			return true;
		};
		if (!GenerateKey(FIntVector(0, 5242, 0), YA) || !GenerateKey(FIntVector(0, 5243, 0), YB)
			|| !GenerateKey(FIntVector(5243, 0, 0), ZA) || !GenerateKey(FIntVector(5243, 0, 1), ZB))
		{
			return false;
		}
		bYSeam = M4FixedOverlapY(*YA, *YB);
		bZSeam = M4FixedOverlapZ(*ZA, *ZB);
		TArray<uint16> Sentinel = {7};
		const FVoxelLandError Cancel =
			VoxelLandM4FixedLattice::Evaluate(*Controls[0], 16777200, 0, 1, 2, 2, Sentinel, [] { return true; });
		bCancelPreserved = Cancel.Code == EVoxelLandErrorCode::Cancelled && Sentinel == TArray<uint16>({7});
		Draft.Instructions[2].Op = EVoxelLandOpcode::Perlin2;
		if (const FVoxelLandError E = FVoxelLandProgram::Validate(Draft, Program))
		{
			Failure = E.Message;
			return false;
		}
		Settings.Program = Program;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Unsupported;
		if (const FVoxelLandError E = FVoxelLandGenerator::Create(Settings, Unsupported))
		{
			Failure = E.Message;
			return false;
		}
		bUnsupported = bool(VoxelLandM4FixedLattice::Validate(*Unsupported));
		return true;
	}).Get();
	if (!TestTrue(TEXT("fixed-lattice fixture completes"), bSuccess))
	{
		AddError(Failure);
		return false;
	}
	TestTrue(TEXT("legacy boundary preserved; v2 accepts farther coordinates"), bLegacyRejected);
	TestTrue(TEXT("positive boundary halo is seamless"), bPositiveSeam);
	TestTrue(TEXT("negative boundary halo is seamless"), bNegativeSeam);
	TestTrue(TEXT("Y boundary halo is seamless"), bYSeam);
	TestTrue(TEXT("Z boundary halo is seamless"), bZSeam);
	TestTrue(TEXT("fixed-lattice result is repeatable"), bRepeat);
	TestTrue(TEXT("unsupported graph fails validation"), bUnsupported);
	TestTrue(TEXT("cancellation leaves output unchanged"), bCancelPreserved);
	return true;
}
#endif
