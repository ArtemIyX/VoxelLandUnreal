#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Structs/VoxelLandM4UnlimitedConfig.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4CurveLut.h"
#include "Generation/VoxelLandM4FixedLattice.h"
#include "Generation/VoxelLandM4Overhang.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace
{
constexpr EAutomationTestFlags M4ShapeReferenceFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
constexpr int64 M4ShapeUnit = int64(1) << 24;

// Reference uses direct signed products and quotient/remainder floor, rather
// than the production split-coordinate calculation. Test coordinates and
// frequencies are selected so all products fit int64.
int64 M4ShapeRound(int64 N, int64 D)
{
	int64 Q = N / D;
	const int64 R = N % D;
	const int64 Magnitude = R < 0 ? -R : R;
	if (Magnitude && Magnitude >= D - Magnitude)
	{
		Q += N < 0 ? -1 : 1;
	}
	return Q;
}
uint64 M4ShapeMix(uint64 X)
{
	X += 0x9e3779b97f4a7c15ull;
	X = (X ^ (X >> 30)) * 0xbf58476d1ce4e5b9ull;
	X = (X ^ (X >> 27)) * 0x94d049bb133111ebull;
	return X ^ (X >> 31);
}
void M4ShapeCoordinate(int64 Position, int64 Frequency, int64& Cell, int64& Fraction)
{
	const int64 Product = Position * Frequency;
	Cell = Product / M4ShapeUnit;
	Fraction = Product % M4ShapeUnit;
	if (Fraction < 0)
	{
		--Cell;
		Fraction += M4ShapeUnit;
	}
}
int64 M4ShapeMul(int64 A, int64 B)
{
	return M4ShapeRound(A * B, M4ShapeUnit);
}
int64 M4ShapeFade(int64 T)
{
	int64 Powers[6] = {1, T, 0, 0, 0, 0};
	for (int32 Index = 2; Index <= 5; ++Index)
	{
		Powers[Index] = M4ShapeMul(Powers[Index - 1], T);
	}
	return FMath::Clamp<int64>(6 * Powers[5] - 15 * Powers[4] + 10 * Powers[3], 0, M4ShapeUnit);
}
int64 M4ShapeCorner(int64 X, int64 Y, int64 Z, uint64 Seed, bool b3D)
{
	const uint64 YHash = M4ShapeMix(uint64(Y));
	uint64 Hash = Seed ^ M4ShapeMix(uint64(X)) ^ ((YHash << 31) | (YHash >> 33));
	if (b3D)
	{
		const uint64 ZHash = M4ShapeMix(uint64(Z));
		Hash ^= (ZHash << 17) | (ZHash >> 47);
	}
	return int64((M4ShapeMix(Hash) >> 39) & 0x1ffffffull) - M4ShapeUnit;
}
int32 M4ShapeReference(int64 X, int64 Y, int64 Z, int64 Frequency, uint64 Seed, bool b3D)
{
	int64 Cell[3], Fraction[3];
	M4ShapeCoordinate(X, Frequency, Cell[0], Fraction[0]);
	M4ShapeCoordinate(Y, Frequency, Cell[1], Fraction[1]);
	M4ShapeCoordinate(Z, Frequency, Cell[2], Fraction[2]);
	const int64 TX = M4ShapeFade(Fraction[0]), TY = M4ShapeFade(Fraction[1]), TZ = M4ShapeFade(Fraction[2]);
	const uint64 MixedSeed = M4ShapeMix(Seed);
	auto Lerp = [](int64 A, int64 B, int64 T) {
		return A + M4ShapeMul(B - A, T);
	};
	auto Plane = [&](int64 PlaneZ) {
		return Lerp(Lerp(M4ShapeCorner(Cell[0], Cell[1], PlaneZ, MixedSeed, b3D),
						M4ShapeCorner(Cell[0] + 1, Cell[1], PlaneZ, MixedSeed, b3D), TX),
			Lerp(M4ShapeCorner(Cell[0], Cell[1] + 1, PlaneZ, MixedSeed, b3D),
				M4ShapeCorner(Cell[0] + 1, Cell[1] + 1, PlaneZ, MixedSeed, b3D), TX),
			TY);
	};
	const int64 Lower = Plane(Cell[2]);
	const int64 Sample = b3D ? Lerp(Lower, Plane(Cell[2] + 1), TZ) : Lower;
	return int32(FMath::Clamp<int64>(M4ShapeRound(Sample * 32767, M4ShapeUnit), -32767, 32767));
}
int32 M4ShapeDisplacement(int32 Amplitude, int32 Band, int64 Height, int32 Noise)
{
	if (Amplitude == 0 || Height <= -int64(Band) || Height >= int64(Band))
	{
		return 0;
	}
	const int64 Magnitude = Height < 0 ? -Height : Height;
	return int32(M4ShapeRound(int64(Amplitude) * (Band - Magnitude) * Noise, int64(Band) * 32767));
}
FVoxelLandInstruction M4ShapeInstruction(EVoxelLandOpcode Op, uint16 Dst = 0, uint16 A = 0, uint32 Imm = 0)
{
	FVoxelLandInstruction I;
	I.Op = Op;
	I.Dst = Dst;
	I.A = A;
	I.Imm = Imm;
	return I;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4ShapeReferenceTest, "VoxelLand.M4.FixedLattice.ShapingReference", M4ShapeReferenceFlags)
bool FVoxelLandM4ShapeReferenceTest::RunTest(const FString& Parameters)
{
	bool bSamples2D = true, bSamples3D = true, bSeeds = false, bFrequency = false, bBand = true;
	bool bFullDensity = false, bZero = false;
	FString Error;
	const bool bFixture = Async(EAsyncExecution::ThreadPool, [&] {
		constexpr int64 FrequencyQ24 = M4ShapeUnit / 8;
		const int64 Coordinates[][3] = {
			{0, 0, 0}, {-1, -9, -17}, {1, 7, 15}, {-13421770000ll, 104851200, -799}, {13421770000ll, -104851200, 1999}};
		const uint64 Seeds[] = {0ull, 0x12345678ull, 0xffffffffffffffffull};
		for (const auto& P : Coordinates)
		{
			for (uint64 Seed : Seeds)
			{
				int32 Actual2D = 0, Actual3D = 0;
				bSamples2D &= VoxelLandM4FixedLattice::SampleShaping2D(P[0], P[1], 0.125f, Seed, Actual2D)
						   && Actual2D == M4ShapeReference(P[0], P[1], 0, FrequencyQ24, Seed, false);
				bSamples3D &= VoxelLandM4FixedLattice::SampleShaping3D(P[0], P[1], P[2], 0.125f, Seed, Actual3D)
						   && Actual3D == M4ShapeReference(P[0], P[1], P[2], FrequencyQ24, Seed, true);
			}
		}
		int32 SeedA = 0, SeedB = 0;
		bSeeds = VoxelLandM4FixedLattice::SampleShaping3D(-123, 456, -789, 0.125f, 1, SeedA)
			  && VoxelLandM4FixedLattice::SampleShaping3D(-123, 456, -789, 0.125f, 2, SeedB) && SeedA != SeedB;
		const float HalfQ24 = 1.f / 33554432.f;
		int32 Tiny = 0, Sentinel = 77;
		bFrequency = VoxelLandM4FixedLattice::SampleShaping2D(100, -200, HalfQ24, 7, Tiny)
				  && Tiny == M4ShapeReference(100, -200, 0, 1, 7, false)
				  && !VoxelLandM4FixedLattice::SampleShaping2D(100, -200, HalfQ24 * 0.5f, 7, Sentinel) && Sentinel == 77
				  && !VoxelLandM4FixedLattice::IsShapingFrequencySupported(1.000001f);
		FVoxelLandM4OverhangSettings Overhang;
		Overhang.bEnabled = true;
		Overhang.AmplitudeQ8 = 32767;
		Overhang.BandHalfWidthQ8 = 65534;
		const int64 Heights[] = {-65535, -65534, -65533, -1, 0, 1, 65533, 65534, 65535,
			std::numeric_limits<int64>::min(), std::numeric_limits<int64>::max()};
		for (int64 Height : Heights)
		{
			for (int32 Noise : {-32767, -1, 0, 1, 32767})
			{
				int32 Actual = 123;
				bBand &= VoxelLandM4Overhang::DisplacementQ8(Overhang, Height, Noise, Actual)
					  && Actual == M4ShapeDisplacement(Overhang.AmplitudeQ8, Overhang.BandHalfWidthQ8, Height, Noise);
			}
		}
		// Height=Band-1 with amplitude=Band/2 is an exact half-Q8 tie.
		int32 PositiveTie = 0, NegativeTie = 0;
		bBand &= VoxelLandM4Overhang::DisplacementQ8(Overhang, 65533, 32767, PositiveTie) && PositiveTie == 1
			  && VoxelLandM4Overhang::DisplacementQ8(Overhang, -65533, -32767, NegativeTie) && NegativeTie == -1;
		FVoxelLandProgramDraft Draft;
		Draft.FormatVersion = 2;
		Draft.RegisterCount = 1;
		Draft.Constants = {0.f};
		Draft.Instructions = {M4ShapeInstruction(EVoxelLandOpcode::LoadConstant, 0, 0, 0),
			M4ShapeInstruction(EVoxelLandOpcode::StoreDensity, 0, 0)};
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (const FVoxelLandError Failure = FVoxelLandProgram::Validate(Draft, Program))
		{
			Error = Failure.Message;
			return false;
		}
		FVoxelLandGeneratorSettings Settings;
		Settings.Program = Program;
		Settings.WorldSeed = 0x123456789abcdef0ull;
		Settings.Profile.VoxelSizeCm = 100.f;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Control;
		if (const FVoxelLandError Failure = FVoxelLandGenerator::Create(Settings, Control))
		{
			Error = Failure.Message;
			return false;
		}
		TArray<int32> Samples;
		Samples.Init(0, 4097);
		TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
		if (!FVoxelLandM4CurveLut::Create(Samples, -512000000, 512000000, Curve, Error))
		{
			return false;
		}
		FVoxelLandM4UnlimitedConfig Config;
		Config.ContinentalGenerator = Config.MountainGenerator = Config.PeaksGenerator = Control;
		Config.ContinentalCurve = Config.MountainCurve = Config.PeaksCurve = Curve;
		auto Generate = [&](const FVoxelLandM4UnlimitedConfig& Input,
							TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& Out) {
			TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
			if (const FVoxelLandError Failure = FVoxelLandGenerator::CreateM4Unlimited(Settings, Input, Generator))
			{
				Error = Failure.Message;
				return false;
			}
			TUniquePtr<FVoxelLandWorkerContext> Worker;
			if (const FVoxelLandError Failure = Generator->CreateWorker(Worker))
			{
				Error = Failure.Message;
				return false;
			}
			FVoxelLandChunkKey Key;
			if (const FVoxelLandError Failure = Generator->GenerateChunk(Key, *Worker, Out))
			{
				Error = Failure.Message;
				return false;
			}
			return Out.IsValid();
		};
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Baseline, Zero, Shaped;
		if (!Generate(Config, Baseline))
		{
			return false;
		}
		Config.HeightDetail.bEnabled = true;
		Config.Overhang.Displacement.bEnabled = true;
		if (!Generate(Config, Zero))
		{
			return false;
		}
		bZero = Baseline->Density == Zero->Density && Baseline->Hash == Zero->Hash;
		Config.HeightDetail.AmplitudeQ8 = 25600;
		Config.HeightDetail.FrequencyPerCm = 0.125f;
		Config.Overhang.Displacement.AmplitudeQ8 = 32767;
		Config.Overhang.Displacement.BandHalfWidthQ8 = 65534;
		Config.Overhang.FrequencyPerCm = 0.125f;
		if (!Generate(Config, Shaped))
		{
			return false;
		}
		const uint64 DetailSeed = uint64(uint32(VoxelLandDeterminism::DeriveNodeSeed(
			Settings.WorldSeed, Config.HeightDetail.NodeId, Config.HeightDetail.Salt)));
		const uint64 OverhangSeed = uint64(uint32(
			VoxelLandDeterminism::DeriveNodeSeed(Settings.WorldSeed, Config.Overhang.NodeId, Config.Overhang.Salt)));
		const int32 Points[][3] = {{0, 0, 0}, {1, 2, 0}, {4, 15, 0}, {16, 31, 1}, {31, 32, 0}};
		bFullDensity = true;
		bool bAnyEffect = false;
		for (const auto& P : Points)
		{
			const int64 X = int64(P[0]) * 100, Y = int64(P[1]) * 100, Z = int64(P[2]) * 100;
			const int32 DetailNoise = M4ShapeReference(X, Y, 0, FrequencyQ24, DetailSeed, false);
			const int64 HeightQ8 = M4ShapeRound(int64(Config.HeightDetail.AmplitudeQ8) * DetailNoise, 32767);
			const int32 OverhangNoise = M4ShapeReference(X, Y, Z, FrequencyQ24, OverhangSeed, true);
			const int64 DensityQ8 = Z * 256 - HeightQ8
								  + M4ShapeDisplacement(Config.Overhang.Displacement.AmplitudeQ8,
									  Config.Overhang.Displacement.BandHalfWidthQ8, Z * 256 - HeightQ8, OverhangNoise);
			const int16 Expected = int16(FMath::Clamp<int64>(M4ShapeRound(DensityQ8 * 4, 100), -32768, 32767));
			const int32 Index = (P[0] + 1) + 35 * ((P[1] + 1) + 35 * (P[2] + 1));
			bFullDensity &= Shaped->Density[Index] == Expected;
			bAnyEffect |= Shaped->Density[Index] != Baseline->Density[Index];
		}
		bFullDensity &= bAnyEffect;
		return true;
	}).Get();
	if (!TestTrue(FString::Printf(TEXT("fixed shaping reference fixture: %s"), *Error), bFixture))
	{
		return false;
	}
	TestTrue(TEXT("2D shaping matches independent integer reference"), bSamples2D);
	TestTrue(TEXT("3D shaping matches independent integer reference"), bSamples3D);
	TestTrue(TEXT("distinct shaping seeds give distinct results"), bSeeds);
	TestTrue(TEXT("frequency rounding and invalid-value behavior match contract"), bFrequency);
	TestTrue(TEXT("overhang band and signed half ties match rational reference"), bBand);
	TestTrue(TEXT("zero-amplitude shaping preserves full chunk"), bZero);
	TestTrue(TEXT("shaped chunk samples match independent height and displacement reference"), bFullDensity);
	return true;
}
#endif
