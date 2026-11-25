#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Terrain/ChunkLocal/VoxelLandChunkLocalTerrainConfig.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Curves/VoxelLandCurveLut.h"
#include "Generation/Terrain/ChunkLocal/VoxelLandFixedLattice.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M4FixedReferenceFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
constexpr int64 M4RefUnit = int64(1) << 24;

// Independent reference for the bounded coordinates in this fixture: direct
// int64 multiplication and floor division, instead of the implementation's
// split-coordinate arithmetic. These products fit the signed 64-bit range.
int64 M4RefRound(int64 Numerator, int64 Denominator)
{
	int64 Whole = Numerator / Denominator;
	const int64 Remainder = Numerator % Denominator;
	const int64 Magnitude = Remainder < 0 ? -Remainder : Remainder;
	if (Magnitude * 2 >= Denominator && Magnitude != 0)
	{
		Whole += Numerator < 0 ? -1 : 1;
	}
	return Whole;
}
uint64 M4RefMix(uint64 Value)
{
	Value += 0x9e3779b97f4a7c15ull;
	Value = (Value ^ (Value >> 30)) * 0xbf58476d1ce4e5b9ull;
	Value = (Value ^ (Value >> 27)) * 0x94d049bb133111ebull;
	return Value ^ (Value >> 31);
}
int64 M4RefCorner(int64 X, int64 Y, uint64 Seed)
{
	const uint64 MixedY = M4RefMix(uint64(Y));
	const uint64 RotatedY = (MixedY << 31) | (MixedY >> 33);
	return int64((M4RefMix(Seed ^ M4RefMix(uint64(X)) ^ RotatedY) >> 39) & 0x1ffffffull) - M4RefUnit;
}
int64 M4RefProduct(int64 A, int64 B)
{
	return M4RefRound(A * B, M4RefUnit);
}
int64 M4RefFade(int64 Fraction)
{
	int64 Power = Fraction;
	int64 Powers[6] = {1, Fraction, 0, 0, 0, 0};
	for (int32 I = 2; I <= 5; ++I)
	{
		Power = M4RefProduct(Power, Fraction);
		Powers[I] = Power;
	}
	return FMath::Clamp<int64>(6 * Powers[5] - 15 * Powers[4] + 10 * Powers[3], 0, M4RefUnit);
}
void M4RefCoordinate(int64 WorldCm, int64 Frequency, int64& Cell, int64& Fraction)
{
	const int64 Product = WorldCm * Frequency;
	Cell = Product / M4RefUnit;
	Fraction = Product % M4RefUnit;
	if (Fraction < 0)
	{
		--Cell;
		Fraction += M4RefUnit;
	}
}
int64 M4RefValue(int64 X, int64 Y, int64 Frequency, uint64 Seed)
{
	int64 CellX, CellY, FractionX, FractionY;
	M4RefCoordinate(X, Frequency, CellX, FractionX);
	M4RefCoordinate(Y, Frequency, CellY, FractionY);
	const int64 FX = M4RefFade(FractionX), FY = M4RefFade(FractionY);
	auto Interpolate = [](int64 A, int64 B, int64 T) {
		return A + M4RefProduct(B - A, T);
	};
	return Interpolate(Interpolate(M4RefCorner(CellX, CellY, Seed), M4RefCorner(CellX + 1, CellY, Seed), FX),
		Interpolate(M4RefCorner(CellX, CellY + 1, Seed), M4RefCorner(CellX + 1, CellY + 1, Seed), FX), FY);
}
uint16 M4RefControl(int64 X, int64 Y, uint64 Seed, int32 Octaves)
{
	int64 Frequency = 336; // round(0.00002 * 2^24)
	int64 Weight = M4RefUnit, WeightSum = 0, Sum = 0;
	for (int32 Octave = 0; Octave < Octaves; ++Octave)
	{
		const int64 Value = M4RefValue(X, Y, Frequency, M4RefMix(Seed + uint64(Octave)));
		Sum += M4RefProduct(Value, Weight);
		WeightSum += Weight;
		Frequency *= 2;
		Weight = M4RefRound(Weight, 2);
	}
	const int64 Value = FMath::Clamp<int64>(M4RefRound(Sum * M4RefUnit, WeightSum), -M4RefUnit, M4RefUnit);
	return uint16(((Value + M4RefUnit) * 65535 + M4RefUnit) / (2 * M4RefUnit));
}
FVoxelLandInstruction M4RefInstruction(EVoxelLandOpcode Op, uint16 Dst = 0, uint16 A = 0, uint16 B = 0, uint32 Imm = 0)
{
	FVoxelLandInstruction I;
	I.Op = Op;
	I.Dst = Dst;
	I.A = A;
	I.B = B;
	I.Imm = Imm;
	return I;
}
bool M4RefOverlap(const FVoxelLandChunkData& Left, const FVoxelLandChunkData& Right)
{
	for (int32 Z = 0; Z < 35; ++Z)
	{
		for (int32 Y = 0; Y < 35; ++Y)
		{
			for (int32 X = 0; X < 3; ++X)
			{
				if (Left.Density[X + 32 + 35 * (Y + 35 * Z)] != Right.Density[X + 35 * (Y + 35 * Z)])
				{
					return false;
				}
			}
		}
	}
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4FixedReferenceTest, "VoxelLand.M4.FixedLattice.IndependentReference", M4FixedReferenceFlags)
bool FVoxelLandM4FixedReferenceTest::RunTest(const FString& Parameters)
{
	bool bNoiseReference = false, bFbmReference = false, bPositiveHalf = false, bNegativeHalf = false;
	bool bPositiveBoundary = false, bNegativeBoundary = false, bBoundaryDemand = false;
	FString Error;
	const bool bFixture = Async(EAsyncExecution::ThreadPool, [&] {
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> FbmProgram;
		auto MakeNoiseProgram = [&](EVoxelLandOpcode Opcode,
									TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& Out) {
			FVoxelLandProgramDraft Draft;
			Draft.FormatVersion = 2;
			Draft.RegisterCount = 3;
			Draft.Constants = {0.00002f, 3.f, 0.5f, 2.f};
			FVoxelLandSeedSlot Seed;
			Seed.NodeId = FGuid(0x1234, 0x5678, 0x9abc, 0xdef0);
			Seed.Salt = 17;
			Draft.Seeds.Add(Seed);
			FVoxelLandNoiseConfig Noise;
			Noise.SeedSlot = 0;
			Noise.Frequency.Index = 0;
			Noise.Octaves.Index = 1;
			Noise.Gain.Index = 2;
			Noise.Lacunarity.Index = 3;
			Draft.Noise.Add(Noise);
			Draft.Instructions = {M4RefInstruction(EVoxelLandOpcode::LoadX, 0),
				M4RefInstruction(EVoxelLandOpcode::LoadY, 1), M4RefInstruction(Opcode, 2, 0, 1),
				M4RefInstruction(EVoxelLandOpcode::StoreDensity, 0, 2)};
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
			if (const FVoxelLandError Failure = FVoxelLandProgram::Validate(Draft, Program))
			{
				Error = Failure.Message;
				return false;
			}
			if (Opcode == EVoxelLandOpcode::Fbm2)
			{
				FbmProgram = Program;
			}
			FVoxelLandGeneratorSettings Settings;
			Settings.Program = Program;
			Settings.WorldSeed = 0x123456789abcdef0ull;
			Settings.Profile.VoxelSizeCm = 100.f;
			if (const FVoxelLandError Failure = FVoxelLandGenerator::Create(Settings, Out))
			{
				Error = Failure.Message;
				return false;
			}
			return true;
		};
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> ValueGenerator, FbmGenerator;
		if (!MakeNoiseProgram(EVoxelLandOpcode::Value2, ValueGenerator)
			|| !MakeNoiseProgram(EVoxelLandOpcode::Fbm2, FbmGenerator))
		{
			return false;
		}
		auto CheckSamples = [&](const FVoxelLandGenerator& Generator, int32 Octaves) {
			const int64 BoundaryCm = int64(32766) * 32 * 100;
			const int64 Origins[][2] = {{-BoundaryCm, -16777300}, {-101, -199}, {-1, 0}, {0, 1}, {16777300, BoundaryCm},
				{BoundaryCm, -BoundaryCm}};
			for (const auto& Origin : Origins)
			{
				TArray<uint16> Samples;
				if (const FVoxelLandError Failure = VoxelLandM4FixedLattice::Evaluate(
						Generator, Origin[0], Origin[1], 100, 2, 2, Samples, [] { return false; }))
				{
					Error = Failure.Message;
					return false;
				}
				if (Samples.Num() != 9)
				{
					return false;
				}
				for (int32 Y = 0; Y <= 2; ++Y)
				{
					for (int32 X = 0; X <= 2; ++X)
					{
						if (Samples[X + 3 * Y]
							!= M4RefControl(
								Origin[0] + X * 100, Origin[1] + Y * 100, Generator.GetResolvedSeeds()[0], Octaves))
						{
							return false;
						}
					}
				}
			}
			return true;
		};
		bNoiseReference = CheckSamples(*ValueGenerator, 1);
		bFbmReference = CheckSamples(*FbmGenerator, 3);
		auto CheckHalf = [&](float Half, uint16 Expected) {
			FVoxelLandProgramDraft Draft;
			Draft.FormatVersion = 2;
			Draft.RegisterCount = 3;
			Draft.Constants = {Half, 64.f};
			Draft.Instructions = {M4RefInstruction(EVoxelLandOpcode::LoadConstant, 0, 0, 0, 0),
				M4RefInstruction(EVoxelLandOpcode::LoadConstant, 1, 0, 0, 1),
				M4RefInstruction(EVoxelLandOpcode::Multiply, 2, 0, 1),
				M4RefInstruction(EVoxelLandOpcode::Multiply, 2, 2, 1),
				M4RefInstruction(EVoxelLandOpcode::StoreDensity, 0, 2)};
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
			if (const FVoxelLandError Failure = FVoxelLandProgram::Validate(Draft, Program))
			{
				Error = Failure.Message;
				return false;
			}
			FVoxelLandGeneratorSettings Settings;
			Settings.Program = Program;
			TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
			if (const FVoxelLandError Failure = FVoxelLandGenerator::Create(Settings, Generator))
			{
				Error = Failure.Message;
				return false;
			}
			TArray<uint16> Samples;
			if (const FVoxelLandError Failure =
					VoxelLandM4FixedLattice::Evaluate(*Generator, -1, 0, 1, 1, 1, Samples, [] { return false; }))
			{
				Error = Failure.Message;
				return false;
			}
			return Samples.Num() == 4 && Samples[0] == Expected && Samples[1] == Expected && Samples[2] == Expected
				&& Samples[3] == Expected;
		};
		bPositiveHalf = CheckHalf(1.f / 33554432.f, 32775);
		bNegativeHalf = CheckHalf(-1.f / 33554432.f, 32760);
		TArray<int32> Lut;
		for (int32 Index = 0; Index <= 4096; ++Index)
		{
			Lut.Add((Index - 2048) * 10);
		}
		TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
		if (!FVoxelLandM4CurveLut::Create(Lut, -20480, 20480, Curve, Error))
		{
			return false;
		}
		FVoxelLandM4UnlimitedConfig Config;
		Config.ContinentalGenerator = Config.MountainGenerator = Config.PeaksGenerator = FbmGenerator;
		Config.ContinentalCurve = Config.MountainCurve = Config.PeaksCurve = Curve;
		Config.bNormalizeSlope = true;
		FVoxelLandM4SmoothSettings Smooth;
		Smooth.bEnabled = true;
		Smooth.Strength = 0.5f;
		Config.SmoothOverride = Smooth;
		FVoxelLandGeneratorSettings FinalSettings;
		FinalSettings.Program = FbmProgram;
		FinalSettings.Profile.VoxelSizeCm = 100.f;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Final;
		if (const FVoxelLandError Failure = FVoxelLandGenerator::CreateM4Unlimited(FinalSettings, Config, Final))
		{
			Error = Failure.Message;
			return false;
		}
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		if (const FVoxelLandError Failure = Final->CreateWorker(Worker))
		{
			Error = Failure.Message;
			return false;
		}
		auto Generate = [&](int32 X, TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& Out) {
			FVoxelLandChunkKey Key;
			Key.Coordinate.X = X;
			if (const FVoxelLandError Failure = Final->GenerateChunk(Key, *Worker, Out))
			{
				Error = Failure.Message;
				return false;
			}
			return bool(Out);
		};
		FVoxelLandChunkKey Positive, Negative;
		Positive.Coordinate.X = 32766;
		Negative.Coordinate.X = -32766;
		bBoundaryDemand = !Final->ValidateM4ChunkDemand(Positive) && !Final->ValidateM4ChunkDemand(Negative);
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> PositiveA, PositiveB, NegativeA, NegativeB;
		if (!Generate(32765, PositiveA) || !Generate(32766, PositiveB) || !Generate(-32766, NegativeA)
			|| !Generate(-32765, NegativeB))
		{
			return false;
		}
		bPositiveBoundary = M4RefOverlap(*PositiveA, *PositiveB);
		bNegativeBoundary = M4RefOverlap(*NegativeA, *NegativeB);
		return true;
	}).Get();
	if (!TestTrue(FString::Printf(TEXT("independent fixed-lattice fixture: %s"), *Error), bFixture))
	{
		return false;
	}
	TestTrue(TEXT("value noise matches independent floor-division reference"), bNoiseReference);
	TestTrue(TEXT("fBm matches independent fixed-point reference"), bFbmReference);
	TestTrue(TEXT("positive half-Q24 rounds away from zero"), bPositiveHalf);
	TestTrue(TEXT("negative half-Q24 rounds away from zero"), bNegativeHalf);
	TestTrue(TEXT("both boundary chunk demands are accepted"), bBoundaryDemand);
	TestTrue(TEXT("positive scheduler boundary overlap matches"), bPositiveBoundary);
	TestTrue(TEXT("negative scheduler boundary overlap matches"), bNegativeBoundary);
	return true;
}
#endif
