#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Structs/VoxelLandM1SampleResult.h"
#include "Data/Structs/VoxelLandM1QuantizationCase.h"
#include "Data/Structs/VoxelLandM1GoldenCase.h"
#include "Data/Structs/VoxelLandM1OperandCase.h"
#include "Data/Structs/VoxelLandM1WorkerProbe.h"
#include "Data/Structs/VoxelLandM1Capture.h"
using namespace VoxelLandM1TestData;

#include "VoxelLandM1Fixtures.h"
#include "Async/Async.h"
#include "Hash/xxhash.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformProperties.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/AutomationTest.h"
#include <limits>

using namespace VoxelLandM1Fixtures;

namespace
{

constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FResult Run(FVoxelLandProgramDraft Draft, FVoxelLandZoneProfile Profile, FVoxelLandChunkKey Key,
	FVoxelLandBackendOptions Options = {}, uint64 Seed = 0)
{
	return Async(EAsyncExecution::ThreadPool, [Draft, Profile, Key, Options, Seed]() {
		return Generate(Draft, Profile, Key, Options, Seed);
	}).Get();
}

FString Describe(const FResult& Result, const TCHAR* Fixture, uint64 Seed, float Size, FVoxelLandChunkKey Key)
{
	return FString::Printf(TEXT("%s platform=%s seed=%llu voxel=%g key=(%d,%d,%d) fingerprint=%llu vm=%d noise=%d: %s"),
		Fixture, ANSI_TO_TCHAR(FPlatformProperties::PlatformName()), Seed, Size, Key.Coordinate.X, Key.Coordinate.Y,
		Key.Coordinate.Z, Result.Generator ? Result.Generator->GetProgram().GetFingerprint() : 0ull, int32(Result.VM),
		int32(Result.NoiseBackend), *Result.Error.Message);
}

FSampleResult SampleAt(
	FVoxelLandProgramDraft Draft, FVoxelLandBackendOptions Options, uint64 Seed, const TArray<FVector3f>& Positions)
{
	FSampleResult R;
	R.bOffGameThread = !IsInGameThread();
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
	R.Error = FVoxelLandProgram::Validate(Draft, Program);
	if (R.Error)
	{
		return R;
	}
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
	R.Error = FVoxelLandGenerator::Create({Program, Seed, FVoxelLandZoneProfile(), Options}, Generator);
	if (R.Error)
	{
		return R;
	}
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	R.Error = Generator->CreateWorker(Worker);
	if (R.Error)
	{
		return R;
	}
	R.VM = Worker->GetVMBackend();
	R.NoiseBackend = Worker->GetNoiseBackend();
	R.Error = Generator->EvaluateSamples(Positions, *Worker, R.Values);
	return R;
}

FSampleResult Sample(FVoxelLandProgramDraft Draft, FVoxelLandBackendOptions Options, uint64 Seed, int32 Count)
{
	TArray<FVector3f> Positions;
	Positions.Reserve(Count);
	for (int32 SampleIndex = 0; SampleIndex < Count; ++SampleIndex)
	{
		Positions.Emplace(float(SampleIndex - 129) * 13.25f, float((SampleIndex * 31) % 257 - 128) * 7.f,
			float((SampleIndex * 11) % 123 - 61) * 5.f);
	}
	return SampleAt(Draft, Options, Seed, Positions);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM1Quantization, "VoxelLand.M1.Quantization", Flags)
bool FVoxelLandM1Quantization::RunTest(const FString& Parameters)
{
	const FCase Cases[] = {{0.f, 0}, {0.5f, 1}, {-0.5f, -1}, {1.5f, 2}, {-1.5f, -2}, {32767.f, 32767}, {32768.f, 32767},
		{-32768.f, -32768}, {-32769.f, -32768}};
	for (const FCase& C : Cases)
	{
		int16 Out = 0;
		const FVoxelLandError E = VoxelLandDeterminism::Quantize(C.Value, 1024.f, Out);
		TestFalse(TEXT("finite quantization succeeds"), bool(E));
		TestEqual(FString::Printf(TEXT("round/saturate %g"), C.Value), int32(Out), int32(C.Expected));
	}
	int16 Out = 0;
	TestTrue(TEXT("NaN rejected"),
		bool(VoxelLandDeterminism::Quantize(std::numeric_limits<float>::quiet_NaN(), 100.f, Out)));
	TestTrue(TEXT("infinity rejected"),
		bool(VoxelLandDeterminism::Quantize(std::numeric_limits<float>::infinity(), 100.f, Out)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM1PlaneGoldens, "VoxelLand.M1.AnalyticPlaneAndPadding", Flags)
bool FVoxelLandM1PlaneGoldens::RunTest(const FString& Parameters)
{
	const FGolden Cases[] = {{100, FIntVector(0, 0, 0)}, {50, FIntVector(-1, 0, -1)}, {100, FIntVector(-2, -1, 1)},
		{50, FIntVector(1, -1, 0)}};
	for (const FGolden& C : Cases)
	{
		FVoxelLandZoneProfile Profile;
		Profile.VoxelSizeCm = float(C.Size);
		FVoxelLandChunkKey Key;
		Key.Coordinate = C.Key;
		const FResult R = Run(Plane(), Profile, Key);
		TestTrue(TEXT("generation ran on a worker"), R.bOffGameThread);
		if (R.Error || !R.Chunk)
		{
			AddError(Describe(R, TEXT("plane"), 0, Profile.VoxelSizeCm, Key));
			continue;
		}
		const TArray<int16> Expected = PlaneReference(Key, C.Size);
		const uint64 ExpectedHash = ReferenceHash(Expected);
		if (Expected != R.Chunk->Density || ExpectedHash != R.Chunk->Hash)
		{
			AddError(FString::Printf(TEXT("%s expected hash=%llu actual hash=%llu first=%s"),
				*Describe(R, TEXT("plane"), 0, Profile.VoxelSizeCm, Key), ExpectedHash, R.Chunk->Hash,
				*FirstDifference(Expected, R.Chunk->Density, Key)));
		}
		TestEqual(TEXT("chunk sample count"), R.Chunk->Density.Num(), FVoxelLandChunkData::SampleCount);
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			FVoxelLandChunkKey Adjacent = Key;
			if (Axis == 0)
			{
				++Adjacent.Coordinate.X;
			}
			else if (Axis == 1)
			{
				++Adjacent.Coordinate.Y;
			}
			else
			{
				++Adjacent.Coordinate.Z;
			}
			const FResult Other = Run(Plane(), Profile, Adjacent);
			if (Other.Error || !Other.Chunk)
			{
				AddError(Describe(Other, TEXT("adjacent"), 0, Profile.VoxelSizeCm, Adjacent));
				continue;
			}
			bool bMatch = true;
			for (int32 FirstAxisIndex = 0; FirstAxisIndex < 35 && bMatch; ++FirstAxisIndex)
			{
				for (int32 SecondAxisIndex = 0; SecondAxisIndex < 35 && bMatch; ++SecondAxisIndex)
				{
					for (int32 PaddingIndex = 0; PaddingIndex < 3; ++PaddingIndex)
					{
						int32 LX = FirstAxisIndex, LY = SecondAxisIndex, LZ = 32 + PaddingIndex, RX = FirstAxisIndex,
							  RY = SecondAxisIndex, RZ = PaddingIndex;
						if (Axis == 0)
						{
							LX = 32 + PaddingIndex;
							LY = FirstAxisIndex;
							LZ = SecondAxisIndex;
							RX = PaddingIndex;
							RY = FirstAxisIndex;
							RZ = SecondAxisIndex;
						}
						if (Axis == 1)
						{
							LX = FirstAxisIndex;
							LY = 32 + PaddingIndex;
							LZ = SecondAxisIndex;
							RX = FirstAxisIndex;
							RY = PaddingIndex;
							RZ = SecondAxisIndex;
						}
						if (R.Chunk->Density[LX + 35 * (LY + 35 * LZ)]
							!= Other.Chunk->Density[RX + 35 * (RY + 35 * RZ)])
						{
							bMatch = false;
							break;
						}
					}
				}
			}
			TestTrue(FString::Printf(TEXT("overlap axis %d key %s voxel %d"), Axis, *Key.Coordinate.ToString(), C.Size),
				bMatch);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM1Validation, "VoxelLand.M1.Validation", Flags)
bool FVoxelLandM1Validation::RunTest(const FString& Parameters)
{
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> P;
	if (IsInGameThread())
	{
		TestEqual(TEXT("game-thread validation rejected"), int32(FVoxelLandProgram::Validate(Plane(), P).Code),
			int32(EVoxelLandErrorCode::GameThreadNotAllowed));
		const TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> ValidProgram =
			Async(EAsyncExecution::ThreadPool, []() {
				TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Result;
				FVoxelLandProgram::Validate(Plane(), Result);
				return Result;
			}).Get();
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
		TestEqual(TEXT("game-thread generator setup rejected"),
			int32(FVoxelLandGenerator::Create({ValidProgram, 0, {}, {}}, Generator).Code),
			int32(EVoxelLandErrorCode::GameThreadNotAllowed));
	}
	const TFunction<FVoxelLandError(FVoxelLandProgramDraft)> Validate = [](FVoxelLandProgramDraft Draft) {
		return Async(EAsyncExecution::ThreadPool, [Draft]() {
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
			return FVoxelLandProgram::Validate(Draft, Program);
		}).Get();
	};
	FVoxelLandProgramDraft Bad = Plane();
	Bad.FormatVersion = 1;
	TestEqual(TEXT("obsolete program format rejected"), int32(Validate(Bad).Code),
		int32(EVoxelLandErrorCode::InvalidProgram));
	Bad = Plane();
	Bad.FormatVersion = 999;
	TestEqual(TEXT("version rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
	Bad = Plane();
	Bad.Instructions[0].Op = static_cast<EVoxelLandOpcode>(65535);
	TestEqual(TEXT("opcode rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
	Bad = Plane();
	Bad.Instructions[2].A = 2;
	TestEqual(
		TEXT("uninitialized register rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
	Bad = Plane();
	Bad.Instructions[2].B = 300;
	TestEqual(TEXT("register bounds rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
	Bad = Plane();
	Bad.Constants[0] = std::numeric_limits<float>::infinity();
	TestEqual(
		TEXT("non-finite constant rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
	Bad = Plane();
	Bad.Instructions.Pop();
	TestEqual(TEXT("missing store rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
	Bad = Plane();
	Bad.Instructions.Add(Op(EVoxelLandOpcode::StoreDensity, 0, 2));
	TestEqual(TEXT("duplicate store rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
	Bad = Plane();
	Bad.Instructions[1].Imm = 99;
	TestEqual(
		TEXT("constant slot bounds rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
	Bad = Plane();
	Bad.Instructions[0].Dst = 99;
	TestEqual(
		TEXT("destination bounds rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
	Bad = Plane();
	Bad.RegisterCount = 0;
	TestEqual(
		TEXT("empty register file rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
	Bad = Shapes();
	Bad.Constants[0] = 0.f;
	TestEqual(
		TEXT("zero sphere radius rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
	Bad = Shapes();
	Bad.Constants[1] = -1.f;
	TestEqual(
		TEXT("negative box extent rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
	Bad = Shapes();
	Bad.Constants[6] = 2.f;
	TestEqual(
		TEXT("non-unit plane normal rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
	Bad = Noise(EVoxelLandOpcode::Fbm3);
	Bad.Noise[0].SeedSlot = 9;
	TestEqual(
		TEXT("noise seed slot bounds rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
	Bad = Noise(EVoxelLandOpcode::Fbm3);
	Bad.Constants[1] = 2.5f;
	TestEqual(
		TEXT("fractional octaves rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
	Bad = Noise(EVoxelLandOpcode::Fbm3);
	Bad.Constants[0] = 0.f;
	TestEqual(TEXT("zero frequency rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
	Bad = Noise(EVoxelLandOpcode::Fbm3);
	Bad.Constants[2] = 1.1f;
	TestEqual(
		TEXT("gain out of bounds rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
	Bad = Noise(EVoxelLandOpcode::Fbm3);
	Bad.Constants[3] = .5f;
	TestEqual(TEXT("lacunarity out of bounds rejected"), int32(Validate(Bad).Code),
		int32(EVoxelLandErrorCode::InvalidProgram));
	Bad = Plane();
	FVoxelLandParameterSlot Duplicate;
	Duplicate.Name = TEXT("Level");
	Bad.Parameters.Add(Duplicate);
	Duplicate.Name = TEXT("LEVEL");
	Bad.Parameters.Add(Duplicate);
	TestEqual(TEXT("duplicate normalized names rejected"), int32(Validate(Bad).Code),
		int32(EVoxelLandErrorCode::InvalidParameter));
	Bad = Noise(EVoxelLandOpcode::Fbm3);
	const FVoxelLandSeedSlot OriginalSeed = Bad.Seeds[0];
	Bad.Seeds.Add(OriginalSeed);
	TestEqual(TEXT("duplicate GUID and salt rejected"), int32(Validate(Bad).Code),
		int32(EVoxelLandErrorCode::InvalidProgram));
	++Bad.Seeds[1].Salt;
	TestFalse(TEXT("same GUID with distinct salt accepted"), bool(Validate(Bad)));
	Bad = Noise(EVoxelLandOpcode::Fbm3);
	Bad.Seeds[0].NodeId = FGuid();
	TestEqual(
		TEXT("invalid seed GUID rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
	Bad = Plane();
	Duplicate.Name = TEXT("invalid name");
	Bad.Parameters.Add(Duplicate);
	TestEqual(TEXT("invalid parameter name rejected"), int32(Validate(Bad).Code),
		int32(EVoxelLandErrorCode::InvalidParameter));
	Bad = Plane();
	Duplicate.Name = TEXT("value");
	Duplicate.FloatDefault = std::numeric_limits<float>::quiet_NaN();
	Bad.Parameters.Add(Duplicate);
	TestEqual(TEXT("non-finite parameter default rejected"), int32(Validate(Bad).Code),
		int32(EVoxelLandErrorCode::InvalidParameter));
	Bad = Plane();
	Bad.Instructions[1] = Op(EVoxelLandOpcode::LoadFloatParameter, 1, 0, 0, 0, 0);
	TestEqual(
		TEXT("parameter slot bounds rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
	Duplicate.FloatDefault = 0.f;
	Duplicate.Type = EVoxelLandParameterType::Int32;
	Bad.Parameters.Add(Duplicate);
	TestEqual(TEXT("wrong load parameter type rejected"), int32(Validate(Bad).Code),
		int32(EVoxelLandErrorCode::InvalidProgram));
	Bad = Noise(EVoxelLandOpcode::Fbm3);
	Bad.Noise[0].Frequency.bParameter = true;
	Bad.Noise[0].Frequency.Index = 0;
	Bad.Parameters.Add(Duplicate);
	TestEqual(TEXT("wrong noise parameter type rejected"), int32(Validate(Bad).Code),
		int32(EVoxelLandErrorCode::InvalidProgram));
	Bad = Noise(EVoxelLandOpcode::Fbm3);
	Bad.Instructions[3].Imm = 20;
	const FVoxelLandError InvalidInstruction = Validate(Bad);
	TestEqual(TEXT("invalid noise config reports instruction"), InvalidInstruction.Instruction, 3);
	TestEqual(
		TEXT("invalid noise config typed"), int32(InvalidInstruction.Code), int32(EVoxelLandErrorCode::InvalidProgram));
	const FOperandCase Operands[] = {{EVoxelLandOpcode::Add, 2, 0}, {EVoxelLandOpcode::Subtract, 2, 0},
		{EVoxelLandOpcode::Multiply, 2, 0}, {EVoxelLandOpcode::Divide, 2, 0}, {EVoxelLandOpcode::Min, 2, 0},
		{EVoxelLandOpcode::Max, 2, 0}, {EVoxelLandOpcode::Abs, 1, 0}, {EVoxelLandOpcode::Negate, 1, 0},
		{EVoxelLandOpcode::Plane, 3, 4}, {EVoxelLandOpcode::Sphere, 3, 0}, {EVoxelLandOpcode::Box, 3, 1},
		{EVoxelLandOpcode::Union, 2, 0}, {EVoxelLandOpcode::SubtractShape, 2, 0}, {EVoxelLandOpcode::Intersect, 2, 0},
		{EVoxelLandOpcode::Perlin2, 2, 0}, {EVoxelLandOpcode::Perlin3, 3, 0}, {EVoxelLandOpcode::Fbm2, 2, 0},
		{EVoxelLandOpcode::Fbm3, 3, 0}};
	for (const FOperandCase& C : Operands)
	{
		const bool bNoise = C.Code == EVoxelLandOpcode::Perlin2 || C.Code == EVoxelLandOpcode::Perlin3
						 || C.Code == EVoxelLandOpcode::Fbm2 || C.Code == EVoxelLandOpcode::Fbm3;
		FVoxelLandProgramDraft Base = bNoise ? Noise(C.Code) : Shapes();
		Base.RegisterCount = 4;
		Base.Instructions = {Op(EVoxelLandOpcode::LoadX, 0), Op(EVoxelLandOpcode::LoadY, 1),
			Op(EVoxelLandOpcode::LoadZ, 2), Op(C.Code, 3, 0, 1, 2, C.Imm), Op(EVoxelLandOpcode::StoreDensity, 0, 3)};
		for (int32 Slot = 0; Slot < C.Reads; ++Slot)
		{
			Bad = Base;
			if (Slot == 0)
			{
				Bad.Instructions[3].A = 65535;
			}
			else if (Slot == 1)
			{
				Bad.Instructions[3].B = 65535;
			}
			else
			{
				Bad.Instructions[3].C = 65535;
			}
			const FVoxelLandError Error = Validate(Bad);
			TestTrue(FString::Printf(TEXT("opcode %d source %d rejected at instruction"), int32(C.Code), Slot),
				Error.Code == EVoxelLandErrorCode::InvalidProgram && Error.Instruction == 3);
		}
		Bad = Base;
		Bad.Instructions[3].Dst = 65535;
		const FVoxelLandError Error = Validate(Bad);
		TestTrue(FString::Printf(TEXT("opcode %d destination rejected at instruction"), int32(C.Code)),
			Error.Code == EVoxelLandErrorCode::InvalidProgram && Error.Instruction == 3);
	}
	FVoxelLandZoneProfile Profile;
	Profile.VoxelSizeCm = 0.f;
	TestEqual(TEXT("zero profile rejected"), int32(Run(Plane(), Profile, {}).Error.Code),
		int32(EVoxelLandErrorCode::InvalidProfile));
	Profile.VoxelSizeCm = 100.f;
	Profile.FloatOverrides.Add(TEXT("unknown"), 1.f);
	TestEqual(TEXT("unknown override rejected"), int32(Run(Plane(), Profile, {}).Error.Code),
		int32(EVoxelLandErrorCode::InvalidParameter));
	Profile.FloatOverrides.Empty();
	FVoxelLandChunkKey Key;
	Key.LOD = 1;
	TestEqual(
		TEXT("LOD rejected"), int32(Run(Plane(), Profile, Key).Error.Code), int32(EVoxelLandErrorCode::UnsupportedLod));
	Profile.VoxelSizeCm = std::numeric_limits<float>::infinity();
	TestEqual(TEXT("infinite voxel size rejected"), int32(Run(Plane(), Profile, {}).Error.Code),
		int32(EVoxelLandErrorCode::InvalidProfile));
	Profile.VoxelSizeCm = std::numeric_limits<float>::quiet_NaN();
	TestEqual(TEXT("NaN voxel size rejected"), int32(Run(Plane(), Profile, {}).Error.Code),
		int32(EVoxelLandErrorCode::InvalidProfile));
	const FSampleResult InvalidPosition = Async(EAsyncExecution::ThreadPool, []() {
		const TArray<FVector3f> Positions = {FVector3f(std::numeric_limits<float>::infinity(), 0.f, 0.f)};
		return SampleAt(Plane(), {}, 0, Positions);
	}).Get();
	TestEqual(TEXT("non-finite coordinate rejected"), int32(InvalidPosition.Error.Code),
		int32(EVoxelLandErrorCode::NonFiniteCoordinate));
	Bad = Plane();
	Bad.Constants[0] = 0.f;
	Bad.Instructions[2].Op = EVoxelLandOpcode::Divide;
	const FSampleResult DivideByZero =
		Async(EAsyncExecution::ThreadPool, [Bad]() { return Sample(Bad, {}, 0, 1); }).Get();
	TestEqual(TEXT("division by zero rejected"), int32(DivideByZero.Error.Code),
		int32(EVoxelLandErrorCode::InvalidMathDomain));
	TestTrue(TEXT("failed evaluation publishes no samples"), DivideByZero.Values.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM1GameThread, "VoxelLand.M1.GameThreadRejection", Flags)
bool FVoxelLandM1GameThread::RunTest(const FString& Parameters)
{
	if (!IsInGameThread())
	{
		AddError(TEXT("This probe must execute on the game thread"));
		return false;
	}
	FProbe Probe;
	FEvent* Ready = FPlatformProcess::GetSynchEventFromPool(false);
	FEvent* Release = FPlatformProcess::GetSynchEventFromPool(false);
	TFuture<void> Future = Async(EAsyncExecution::Thread, [&Probe, Ready, Release]() {
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		Probe.Setup = FVoxelLandProgram::Validate(Plane(), Program);
		if (!Probe.Setup)
		{
			Probe.Setup = FVoxelLandGenerator::Create({Program, 0, {}, {}}, Probe.Generator);
		}
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		if (!Probe.Setup)
		{
			Probe.Setup = Probe.Generator->CreateWorker(Worker);
		}
		Probe.Worker = Worker.Get();
		Ready->Trigger();
		Release->Wait();
		// Worker must be destroyed on this same thread.
	});
	Ready->Wait();
	if (Probe.Setup || !Probe.Worker)
	{
		AddError(FString::Printf(TEXT("worker setup failed: %s"), *Probe.Setup.Message));
	}
	else
	{
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk;
		TestEqual(TEXT("game-thread chunk generation rejected"),
			int32(Probe.Generator->GenerateChunk({}, *Probe.Worker, Chunk).Code),
			int32(EVoxelLandErrorCode::GameThreadNotAllowed));
		TestFalse(TEXT("rejected request publishes no chunk"), Chunk.IsValid());
		TArray<int16> Out;
		const FVector3f Position(0.f, 0.f, 0.f);
		TestEqual(TEXT("game-thread sample evaluation rejected"),
			int32(Probe.Generator->EvaluateSamples(MakeArrayView(&Position, 1), *Probe.Worker, Out).Code),
			int32(EVoxelLandErrorCode::GameThreadNotAllowed));
		TUniquePtr<FVoxelLandWorkerContext> WrongWorker;
		TestEqual(TEXT("game-thread worker setup rejected"), int32(Probe.Generator->CreateWorker(WrongWorker).Code),
			int32(EVoxelLandErrorCode::GameThreadNotAllowed));
	}
	Release->Trigger();
	Future.Get();
	FPlatformProcess::ReturnSynchEventToPool(Ready);
	FPlatformProcess::ReturnSynchEventToPool(Release);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM1Backends, "VoxelLand.M1.SimdAndNativeParity", Flags)
bool FVoxelLandM1Backends::RunTest(const FString& Parameters)
{
	const EVoxelLandOpcode Ops[] = {
		EVoxelLandOpcode::Perlin2, EVoxelLandOpcode::Perlin3, EVoxelLandOpcode::Fbm2, EVoxelLandOpcode::Fbm3};
	const int32 Counts[] = {1, 2, 3, 256};
	const uint64 Seeds[] = {0ull, 0xffffffffffffffffull, 0x8000000000000000ull};
	for (EVoxelLandOpcode OpCode : Ops)
	{
		for (uint64 Seed : Seeds)
		{
			for (int32 Count : Counts)
			{
				FVoxelLandBackendOptions Scalar;
				Scalar.VM = EVoxelLandVMBackend::Scalar;
				FVoxelLandBackendOptions Auto;
				const FSampleResult A = Async(EAsyncExecution::ThreadPool, [OpCode, Scalar, Seed, Count]() {
					return Sample(Noise(OpCode), Scalar, Seed, Count);
				}).Get();
				const FSampleResult B = Async(EAsyncExecution::ThreadPool, [OpCode, Auto, Seed, Count]() {
					return Sample(Noise(OpCode), Auto, Seed, Count);
				}).Get();
				const FString Context =
					FString::Printf(TEXT("platform=%s opcode=%d seed=%llu count=%d scalar-noise=%d auto-noise=%d"),
						ANSI_TO_TCHAR(FPlatformProperties::PlatformName()), int32(OpCode), Seed, Count,
						int32(A.NoiseBackend), int32(B.NoiseBackend));
				if (A.Error || B.Error)
				{
					AddError(
						FString::Printf(TEXT("%s: scalar=%s auto=%s"), *Context, *A.Error.Message, *B.Error.Message));
					continue;
				}
				TestTrue(Context + TEXT(" worker thread"), A.bOffGameThread && B.bOffGameThread);
				TestEqual(Context + TEXT(" scalar selected"), int32(A.VM), int32(EVoxelLandVMBackend::Scalar));
				if (A.Values != B.Values)
				{
					for (int32 SampleIndex = 0; SampleIndex < FMath::Min(A.Values.Num(), B.Values.Num()); ++SampleIndex)
					{
						if (A.Values[SampleIndex] != B.Values[SampleIndex])
						{
							AddError(FString::Printf(TEXT("%s sample=%d expected=%d actual=%d"), *Context, SampleIndex,
								A.Values[SampleIndex], B.Values[SampleIndex]));
							break;
						}
					}
				}
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM1Fallback, "VoxelLand.M1.VMFallback", Flags)
bool FVoxelLandM1Fallback::RunTest(const FString& Parameters)
{
	FVoxelLandBackendOptions Forced;
	Forced.VM = EVoxelLandVMBackend::Simd4;
	const FSampleResult Auto = Async(EAsyncExecution::ThreadPool, []() { return Sample(Plane(), {}, 0, 256); }).Get();
	const FSampleResult SIMD =
		Async(EAsyncExecution::ThreadPool, [Forced]() { return Sample(Plane(), Forced, 0, 256); }).Get();
	if (Auto.Error)
	{
		AddError(FString::Printf(TEXT("automatic VM setup failed: %s"), *Auto.Error.Message));
		return false;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("VoxelLandM1ExpectScalarVM")))
	{
		TestEqual(TEXT("compiled-out automatic VM selects scalar"), int32(Auto.VM), int32(EVoxelLandVMBackend::Scalar));
		TestEqual(TEXT("compiled-out forced SIMD is unavailable"), int32(SIMD.Error.Code),
			int32(EVoxelLandErrorCode::BackendUnavailable));
	}
	else if (SIMD.Error)
	{
		TestEqual(TEXT("unsupported forced SIMD is typed unavailable"), int32(SIMD.Error.Code),
			int32(EVoxelLandErrorCode::BackendUnavailable));
		TestEqual(TEXT("unsupported automatic SIMD falls back"), int32(Auto.VM), int32(EVoxelLandVMBackend::Scalar));
	}
	else
	{
		TestEqual(TEXT("forced SIMD selected"), int32(SIMD.VM), int32(EVoxelLandVMBackend::Simd4));
		TestEqual(TEXT("automatic SIMD selected"), int32(Auto.VM), int32(EVoxelLandVMBackend::Simd4));
		TestTrue(TEXT("forced and automatic SIMD agree"), SIMD.Values == Auto.Values);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM1Arithmetic, "VoxelLand.M1.AnalyticArithmetic", Flags)
bool FVoxelLandM1Arithmetic::RunTest(const FString& Parameters)
{
	FVoxelLandProgramDraft P;
	P.RegisterCount = 8;
	P.Constants = {20.f, 2.f, 30.f};
	P.Instructions = {Op(EVoxelLandOpcode::LoadX, 0), Op(EVoxelLandOpcode::Abs, 1, 0),
		Op(EVoxelLandOpcode::LoadConstant, 2, 0, 0, 0, 0), Op(EVoxelLandOpcode::Add, 3, 1, 2),
		Op(EVoxelLandOpcode::LoadConstant, 4, 0, 0, 0, 1), Op(EVoxelLandOpcode::Divide, 3, 3, 4),
		Op(EVoxelLandOpcode::LoadConstant, 5, 0, 0, 0, 2), Op(EVoxelLandOpcode::Subtract, 3, 3, 5),
		Op(EVoxelLandOpcode::Negate, 3, 3), Op(EVoxelLandOpcode::Min, 6, 3, 0), Op(EVoxelLandOpcode::Max, 7, 3, 0),
		Op(EVoxelLandOpcode::SubtractShape, 6, 6, 7), Op(EVoxelLandOpcode::StoreDensity, 0, 6)};
	for (int32 Count : {1, 2, 3, 256})
	{
		FVoxelLandBackendOptions Scalar;
		Scalar.VM = EVoxelLandVMBackend::Scalar;
		for (FVoxelLandBackendOptions Options : {Scalar, FVoxelLandBackendOptions()})
		{
			const FSampleResult R = Async(EAsyncExecution::ThreadPool, [P, Options, Count]() {
				return Sample(P, Options, 0, Count);
			}).Get();
			if (R.Error)
			{
				AddError(FString::Printf(TEXT("arithmetic count=%d: %s"), Count, *R.Error.Message));
				continue;
			}
			TestEqual(TEXT("sample output count"), R.Values.Num(), Count);
			for (int32 SampleIndex = 0; SampleIndex < R.Values.Num(); ++SampleIndex)
			{
				const float X = float(SampleIndex - 129) * 13.25f;
				const float A = -((FMath::Abs(X) + 20.f) / 2.f - 30.f);
				const double Scaled = double(FMath::Max(FMath::Min(A, X), -FMath::Max(A, X))) * 1024.0 / 100.0;
				const double Rounded =
					Scaled >= 0 ? FMath::FloorToDouble(Scaled + 0.5) : FMath::CeilToDouble(Scaled - 0.5);
				const int32 Expected = int32(FMath::Clamp(Rounded, -32768.0, 32767.0));
				if (R.Values[SampleIndex] != Expected)
				{
					AddError(FString::Printf(TEXT("arithmetic backend=%d count=%d position=%g expected=%d actual=%d"),
						int32(R.VM), Count, X, Expected, int32(R.Values[SampleIndex])));
					break;
				}
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM1Shapes, "VoxelLand.M1.AnalyticShapes", Flags)
bool FVoxelLandM1Shapes::RunTest(const FString& Parameters)
{
	const FResult R = Run(Shapes(), {}, {});
	if (R.Error || !R.Chunk)
	{
		AddError(Describe(R, TEXT("shape union/intersection"), 0, 100.f, {}));
		return false;
	}
	const TFunction<int16(int32, int32, int32)> At = [&R](int32 X, int32 Y, int32 Z) {
		return R.Chunk->Density[X + 35 * (Y + 35 * Z)];
	};
	TestEqual(TEXT("shape at origin intersects plane"), int32(At(1, 1, 1)), 0);
	TestEqual(TEXT("interior at z=-100"), int32(At(1, 1, 0)), -1024);
	TestEqual(TEXT("plane cuts shape at z=100"), int32(At(1, 1, 2)), 1024);
	FVoxelLandProgramDraft Sphere;
	Sphere.RegisterCount = 5;
	Sphere.Constants = {100.f, 250.f};
	Sphere.Instructions = {Op(EVoxelLandOpcode::LoadX, 0), Op(EVoxelLandOpcode::LoadY, 1),
		Op(EVoxelLandOpcode::LoadZ, 2), Op(EVoxelLandOpcode::LoadConstant, 3, 0, 0, 0, 0),
		Op(EVoxelLandOpcode::Subtract, 0, 0, 3), Op(EVoxelLandOpcode::Sphere, 4, 0, 1, 2, 1),
		Op(EVoxelLandOpcode::StoreDensity, 0, 4)};
	FVoxelLandProgramDraft Box;
	Box.RegisterCount = 4;
	Box.Constants = {150.f, 200.f, 250.f};
	Box.Instructions = {Op(EVoxelLandOpcode::LoadX, 0), Op(EVoxelLandOpcode::LoadY, 1), Op(EVoxelLandOpcode::LoadZ, 2),
		Op(EVoxelLandOpcode::Box, 3, 0, 1, 2), Op(EVoxelLandOpcode::StoreDensity, 0, 3)};
	const TArray<FVector3f> SpherePositions = {
		FVector3f(100.f, 0.f, 0.f), FVector3f(400.f, 0.f, 0.f), FVector3f(-150.f, 0.f, 0.f)};
	const TArray<FVector3f> BoxPositions = {
		FVector3f(0.f, 0.f, 0.f), FVector3f(200.f, 0.f, 0.f), FVector3f(150.f, 200.f, 250.f)};
	const FSampleResult SphereSamples = Async(EAsyncExecution::ThreadPool, [Sphere, SpherePositions]() {
		return SampleAt(Sphere, {}, 0, SpherePositions);
	}).Get();
	const FSampleResult BoxSamples =
		Async(EAsyncExecution::ThreadPool, [Box, BoxPositions]() { return SampleAt(Box, {}, 0, BoxPositions); }).Get();
	if (SphereSamples.Error || BoxSamples.Error)
	{
		AddError(FString::Printf(
			TEXT("analytic shape samples: %s; %s"), *SphereSamples.Error.Message, *BoxSamples.Error.Message));
	}
	else
	{
		TestTrue(TEXT("translated sphere exact center, outside and surface"),
			SphereSamples.Values == TArray<int16>({-2560, 512, 0}));
		TestTrue(TEXT("box exact interior, outside and corner"), BoxSamples.Values == TArray<int16>({-1536, 512, 0}));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM1Parameters, "VoxelLand.M1.ParametersAndSnapshots", Flags)
bool FVoxelLandM1Parameters::RunTest(const FString& Parameters)
{
	FVoxelLandProgramDraft Draft = Plane();
	FVoxelLandParameterSlot Height;
	Height.Name = TEXT("Height");
	Height.FloatDefault = 500.f;
	Draft.Parameters.Add(Height);
	Draft.Instructions[1] = Op(EVoxelLandOpcode::LoadFloatParameter, 1, 0, 0, 0, 0);
	FVoxelLandZoneProfile Profile;
	Profile.FloatOverrides.Add(TEXT("HEIGHT"), 100.f);
	const FResult R = Run(Draft, Profile, {});
	if (R.Error || !R.Chunk || !R.Generator)
	{
		AddError(Describe(R, TEXT("parameter"), 0, 100.f, {}));
		return false;
	}
	TestEqual(TEXT("float override reaches surface"), int32(R.Chunk->Density[1 + 35 * (1 + 35 * 2)]), 0);
	TestEqual(TEXT("snapshot retains profile size"), R.Generator->GetProfile().VoxelSizeCm, 100.f);
	Profile.VoxelSizeCm = 50.f;
	Profile.FloatOverrides[TEXT("HEIGHT")] = 200.f;
	Draft.Parameters[0].FloatDefault = 999.f;
	TestEqual(TEXT("snapshot unaffected by caller mutation"), R.Generator->GetProfile().VoxelSizeCm, 100.f);
	TestEqual(TEXT("program unaffected by draft mutation"),
		R.Generator->GetProgram().GetData().Parameters[0].FloatDefault, 500.f);

	FVoxelLandZoneProfile Wrong;
	Wrong.IntOverrides.Add(TEXT("height"), 2);
	TestEqual(TEXT("wrong override type rejected"), int32(Run(Draft, Wrong, {}).Error.Code),
		int32(EVoxelLandErrorCode::InvalidParameter));
	Wrong.FloatOverrides.Add(TEXT("HEIGHT"), 2.f);
	TestEqual(TEXT("both override maps rejected"), int32(Run(Draft, Wrong, {}).Error.Code),
		int32(EVoxelLandErrorCode::InvalidParameter));
	Wrong.IntOverrides.Empty();
	Wrong.FloatOverrides[TEXT("HEIGHT")] = std::numeric_limits<float>::infinity();
	TestEqual(TEXT("non-finite override rejected"), int32(Run(Draft, Wrong, {}).Error.Code),
		int32(EVoxelLandErrorCode::InvalidParameter));

	FVoxelLandProgramDraft NoiseDraft = Noise(EVoxelLandOpcode::Fbm3);
	FVoxelLandParameterSlot Octaves;
	Octaves.Name = TEXT("Octaves");
	Octaves.Type = EVoxelLandParameterType::Int32;
	Octaves.IntDefault = 1;
	NoiseDraft.Parameters.Add(Octaves);
	NoiseDraft.Noise[0].Octaves.bParameter = true;
	NoiseDraft.Noise[0].Octaves.Index = 0;
	FVoxelLandZoneProfile One, Four;
	Four.IntOverrides.Add(TEXT("octaves"), 4);
	const FResult A = Run(NoiseDraft, One, {}, {}, 123);
	const FResult B = Run(NoiseDraft, Four, {}, {}, 123);
	if (A.Error || B.Error || !A.Chunk || !B.Chunk)
	{
		AddError(FString::Printf(TEXT("octave override setup: %s; %s"), *A.Error.Message, *B.Error.Message));
	}
	else
	{
		TestTrue(TEXT("int32 octave override changes fBm"), A.Chunk->Density != B.Chunk->Density);
	}
	Four.IntOverrides[TEXT("octaves")] = 0;
	TestTrue(TEXT("invalid octave override rejected"), bool(Run(NoiseDraft, Four, {}).Error));
	NoiseDraft = Noise(EVoxelLandOpcode::Fbm3);
	FVoxelLandParameterSlot Frequency;
	Frequency.Name = TEXT("frequency");
	Frequency.FloatDefault = .002f;
	FVoxelLandParameterSlot Gain;
	Gain.Name = TEXT("gain");
	Gain.FloatDefault = .5f;
	FVoxelLandParameterSlot Lacunarity;
	Lacunarity.Name = TEXT("lacunarity");
	Lacunarity.FloatDefault = 2.f;
	NoiseDraft.Parameters = {Frequency, Gain, Lacunarity};
	NoiseDraft.Noise[0].Frequency = {true, 0};
	NoiseDraft.Noise[0].Gain = {true, 1};
	NoiseDraft.Noise[0].Lacunarity = {true, 2};
	FVoxelLandZoneProfile DefaultNoise, ChangedNoise;
	ChangedNoise.FloatOverrides.Add(TEXT("frequency"), .006f);
	const FResult DefaultChunk = Run(NoiseDraft, DefaultNoise, {}, {}, 456),
				  FrequencyChunk = Run(NoiseDraft, ChangedNoise, {}, {}, 456);
	if (DefaultChunk.Error || FrequencyChunk.Error || !DefaultChunk.Chunk || !FrequencyChunk.Chunk)
	{
		AddError(FString::Printf(
			TEXT("frequency override setup: %s; %s"), *DefaultChunk.Error.Message, *FrequencyChunk.Error.Message));
	}
	else
	{
		TestTrue(TEXT("float frequency override changes noise"),
			DefaultChunk.Chunk->Density != FrequencyChunk.Chunk->Density);
	}
	ChangedNoise.FloatOverrides.Empty();
	ChangedNoise.FloatOverrides.Add(TEXT("frequency"), 0.f);
	TestEqual(TEXT("non-positive frequency override rejected"), int32(Run(NoiseDraft, ChangedNoise, {}).Error.Code),
		int32(EVoxelLandErrorCode::InvalidParameter));
	ChangedNoise.FloatOverrides.Empty();
	ChangedNoise.FloatOverrides.Add(TEXT("gain"), 1.1f);
	TestEqual(TEXT("invalid gain override rejected"), int32(Run(NoiseDraft, ChangedNoise, {}).Error.Code),
		int32(EVoxelLandErrorCode::InvalidParameter));
	ChangedNoise.FloatOverrides.Empty();
	ChangedNoise.FloatOverrides.Add(TEXT("lacunarity"), .5f);
	TestEqual(TEXT("invalid lacunarity override rejected"), int32(Run(NoiseDraft, ChangedNoise, {}).Error.Code),
		int32(EVoxelLandErrorCode::InvalidParameter));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM1Canonical, "VoxelLand.M1.CanonicalHashes", Flags)
bool FVoxelLandM1Canonical::RunTest(const FString& Parameters)
{
	const TArray<int16> Samples = {0, 1, -1, 32767, -32768};
	const uint8 Bytes[] = {0x00, 0x00, 0x01, 0x00, 0xff, 0xff, 0xff, 0x7f, 0x00, 0x80};
	const uint64 Expected = FXxHash64::HashBuffer(Bytes, sizeof(Bytes)).Hash;
	TestEqual(
		TEXT("density hashes canonical little-endian bytes"), VoxelLandDeterminism::HashDensity(Samples), Expected);

	FVoxelLandProgramDraft P = Plane();
	FVoxelLandParameterSlot Height;
	Height.Name = TEXT("Height");
	Height.FloatDefault = 500.f;
	FVoxelLandParameterSlot Other;
	Other.Name = TEXT("Other");
	Other.FloatDefault = 0.f;
	P.Parameters = {Height, Other};
	P.Instructions[1] = Op(EVoxelLandOpcode::LoadFloatParameter, 1);
	FVoxelLandProgramDraft Same = P;
	Same.Parameters[0].Name = TEXT("hEIGHT");
	Same.Parameters[1].FloatDefault = -0.f;
	FVoxelLandZoneProfile One, Two;
	One.FloatOverrides.Add(TEXT("height"), 100.f);
	One.FloatOverrides.Add(TEXT("other"), 5.f);
	Two.FloatOverrides.Add(TEXT("OTHER"), 5.f);
	Two.FloatOverrides.Add(TEXT("HEIGHT"), 100.f);
	const FResult A = Run(P, One, {}), B = Run(Same, Two, {});
	if (A.Error || B.Error || !A.Generator || !B.Generator || !A.Chunk || !B.Chunk)
	{
		AddError(FString::Printf(TEXT("canonical setup: %s; %s"), *A.Error.Message, *B.Error.Message));
		return false;
	}
	TestEqual(TEXT("case and signed zero normalize in fingerprint"), A.Generator->GetProgram().GetFingerprint(),
		B.Generator->GetProgram().GetFingerprint());
	TestTrue(TEXT("override insertion order and case do not change density"),
		A.Chunk->Density == B.Chunk->Density && A.Chunk->Hash == B.Chunk->Hash);
	Same.Parameters[0].FloatDefault = 501.f;
	const FResult Changed = Run(Same, Two, {});
	if (Changed.Error || !Changed.Generator)
	{
		AddError(Changed.Error.Message);
	}
	else
	{
		TestTrue(TEXT("parameter default contributes to program fingerprint"),
			A.Generator->GetProgram().GetFingerprint() != Changed.Generator->GetProgram().GetFingerprint());
	}
	FVoxelLandProgramDraft NoiseA = Noise(EVoxelLandOpcode::Fbm2), NoiseB = NoiseA;
	++NoiseB.Seeds[0].Salt;
	const FResult SeedA = Run(NoiseA, {}, {}), SeedB = Run(NoiseB, {}, {});
	if (SeedA.Error || SeedB.Error || !SeedA.Generator || !SeedB.Generator)
	{
		AddError(FString::Printf(TEXT("seed fingerprint setup: %s; %s"), *SeedA.Error.Message, *SeedB.Error.Message));
	}
	else
	{
		TestTrue(TEXT("seed salt contributes to fingerprint"),
			SeedA.Generator->GetProgram().GetFingerprint() != SeedB.Generator->GetProgram().GetFingerprint());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM1Concurrency, "VoxelLand.M1.SeedsAndWorkers", Flags)
bool FVoxelLandM1Concurrency::RunTest(const FString& Parameters)
{
	FVoxelLandChunkKey Key;
	Key.Coordinate = FIntVector(-1, 2, -1);
	constexpr uint64 Seed = 0xabcdef0123456789ull;
	const FResult First = Run(Noise(EVoxelLandOpcode::Fbm3), {}, Key, {}, Seed);
	if (First.Error || !First.Chunk || !First.Generator)
	{
		AddError(Describe(First, TEXT("fbm3"), Seed, 100.f, Key));
		return false;
	}
	TArray<TFuture<FResult>> Futures;
	for (int32 WorkerIndex = 0; WorkerIndex < 4; ++WorkerIndex)
	{
		const TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Shared = First.Generator;
		Futures.Add(Async(EAsyncExecution::ThreadPool, [Shared, Key]() { return GenerateShared(Shared, Key); }));
	}
	for (TFuture<FResult>& Future : Futures)
	{
		const FResult Other = Future.Get();
		if (Other.Error || !Other.Chunk)
		{
			AddError(Describe(Other, TEXT("concurrent fbm3"), Seed, 100.f, Key));
			continue;
		}
		TestTrue(TEXT("worker ran off game thread"), Other.bOffGameThread);
		if (First.Chunk->Density != Other.Chunk->Density || First.Chunk->Hash != Other.Chunk->Hash)
		{
			AddError(FString::Printf(TEXT("shared generator expected=%llu actual=%llu first=%s"), First.Chunk->Hash,
				Other.Chunk->Hash, *FirstDifference(First.Chunk->Density, Other.Chunk->Density, Key)));
		}
	}
	const FIntVector Keys[] = {FIntVector(0, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, -1)};
	TArray<FResult> Serial;
	for (FIntVector Coordinate : Keys)
	{
		FVoxelLandChunkKey K;
		K.Coordinate = Coordinate;
		const TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Shared = First.Generator;
		Serial.Add(Async(EAsyncExecution::ThreadPool, [Shared, K]() { return GenerateShared(Shared, K); }).Get());
	}
	TArray<TFuture<FResult>> Shuffled;
	for (int32 KeyIndex = 2; KeyIndex >= 0; --KeyIndex)
	{
		FVoxelLandChunkKey K;
		K.Coordinate = Keys[KeyIndex];
		const TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Shared = First.Generator;
		Shuffled.Add(Async(EAsyncExecution::ThreadPool, [Shared, K]() { return GenerateShared(Shared, K); }));
	}
	for (int32 ResultIndex = 0; ResultIndex < Shuffled.Num(); ++ResultIndex)
	{
		const FResult Other = Shuffled[ResultIndex].Get();
		const FResult& Expected = Serial[2 - ResultIndex];
		if (Expected.Error || Other.Error || !Expected.Chunk || !Other.Chunk)
		{
			AddError(
				FString::Printf(TEXT("ordered generation: %s; %s"), *Expected.Error.Message, *Other.Error.Message));
		}
		else
		{
			TestTrue(TEXT("order and concurrency preserve chunk bytes and hash"),
				Expected.Chunk->Hash == Other.Chunk->Hash && Expected.Chunk->Density == Other.Chunk->Density);
		}
	}
	FVoxelLandProgramDraft Extended = Noise(EVoxelLandOpcode::Fbm3);
	FVoxelLandSeedSlot Unused;
	Unused.NodeId = FGuid(11, 22, 33, 44);
	Unused.Salt = 55;
	Extended.Seeds.Add(Unused);
	const FResult Appended = Run(Extended, {}, Key, {}, Seed);
	if (Appended.Error || !Appended.Chunk)
	{
		AddError(Describe(Appended, TEXT("unused seed"), Seed, 100.f, Key));
	}
	else
	{
		TestTrue(
			TEXT("unrelated seed does not change existing noise"), First.Chunk->Density == Appended.Chunk->Density);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM1UnsupportedNativeScalar, "VoxelLand.M1.UnsupportedNativeScalar", Flags)
bool FVoxelLandM1UnsupportedNativeScalar::RunTest(const FString& Parameters)
{
	FVoxelLandBackendOptions Options;
	Options.Noise = EVoxelLandNoiseBackend::Scalar;
	const FSampleResult Result = Async(EAsyncExecution::ThreadPool, [Options]() {
		return Sample(Noise(EVoxelLandOpcode::Perlin2), Options, 0, 1);
	}).Get();
	TestEqual(TEXT("native scalar explicitly rejected"), int32(Result.Error.Code),
		int32(EVoxelLandErrorCode::BackendUnavailable));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM1CrossHostCapture, "VoxelLand.M1.CrossHostCapture", Flags)
bool FVoxelLandM1CrossHostCapture::RunTest(const FString& Parameters)
{
	FString CapturePath, ComparePath;
	const bool bCapture = FParse::Value(FCommandLine::Get(), TEXT("VoxelLandM1Capture="), CapturePath);
	const bool bCompare = FParse::Value(FCommandLine::Get(), TEXT("VoxelLandM1Compare="), ComparePath);
	if (!bCapture && !bCompare)
	{
		AddWarning(TEXT(
			"Cross-host noise comparison is pending; supply -VoxelLandM1Capture=<path> or -VoxelLandM1Compare=<path> as documented."));
		return true;
	}
	const FCapture Captured = Async(EAsyncExecution::ThreadPool, []() {
		FCapture Result;
		Result.bOffGameThread = !IsInGameThread();
		Result.Text.Reserve(6000000);
		const EVoxelLandOpcode Ops[] = {
			EVoxelLandOpcode::Perlin2, EVoxelLandOpcode::Perlin3, EVoxelLandOpcode::Fbm2, EVoxelLandOpcode::Fbm3};
		const int32 Sizes[] = {50, 100};
		const FIntVector Keys[] = {FIntVector(0, 0, 0), FIntVector(-1, 2, -1)};
		const uint64 Seeds[] = {0ull, 0xffffffffffffffffull};
		for (EVoxelLandOpcode Code : Ops)
		{
			for (int32 Size : Sizes)
			{
				for (int32 KeyIndex = 0; KeyIndex < 2; ++KeyIndex)
				{
					FVoxelLandChunkKey Key;
					Key.Coordinate = Keys[KeyIndex];
					FVoxelLandZoneProfile Profile;
					Profile.VoxelSizeCm = float(Size);
					const FResult R = Generate(Noise(Code), Profile, Key, {}, Seeds[KeyIndex]);
					if (R.Error || !R.Chunk)
					{
						Result.Error = Describe(R, TEXT("cross-host capture"), Seeds[KeyIndex], float(Size), Key);
						return Result;
					}
					if (Result.Backends.IsEmpty())
					{
						Result.Backends =
							FString::Printf(TEXT("platform=%s VM=%d native=%d (0=auto, 1=scalar, 2=SIMD4/SSE2)"),
								ANSI_TO_TCHAR(FPlatformProperties::PlatformName()), int32(R.VM), int32(R.NoiseBackend));
					}
					// Stable line format: opcode;voxel size;seed;chunk X;Y;Z;program fingerprint;hash|densities.
					Result.Text += FString::Printf(TEXT("%d;%d;%llu;%d;%d;%d;%llu;%llu|"), int32(Code), Size,
						Seeds[KeyIndex], Key.Coordinate.X, Key.Coordinate.Y, Key.Coordinate.Z,
						R.Generator->GetProgram().GetFingerprint(), R.Chunk->Hash);
					for (int32 SampleIndex = 0; SampleIndex < R.Chunk->Density.Num(); ++SampleIndex)
					{
						if (SampleIndex)
						{
							Result.Text.AppendChar(TEXT(','));
						}
						Result.Text += FString::FromInt(int32(R.Chunk->Density[SampleIndex]));
					}
					Result.Text.AppendChar(TEXT('\n'));
				}
			}
		}
		return Result;
	}).Get();
	TestTrue(TEXT("capture ran on worker"), Captured.bOffGameThread);
	if (!Captured.Error.IsEmpty())
	{
		AddError(Captured.Error);
		return false;
	}
	AddInfo(Captured.Backends);
	if (bCapture
		&& !FFileHelper::SaveStringToFile(
			Captured.Text, *CapturePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		AddError(FString::Printf(TEXT("Could not write capture %s"), *CapturePath));
	}
	if (bCompare)
	{
		FString ExpectedText;
		if (!FFileHelper::LoadFileToString(ExpectedText, *ComparePath))
		{
			AddError(FString::Printf(TEXT("Could not read capture %s"), *ComparePath));
			return false;
		}
		TArray<FString> ExpectedLines, ActualLines;
		ExpectedText.ParseIntoArrayLines(ExpectedLines, true);
		Captured.Text.ParseIntoArrayLines(ActualLines, true);
		if (ExpectedLines.Num() != ActualLines.Num())
		{
			AddError(FString::Printf(
				TEXT("capture case count expected=%d actual=%d"), ExpectedLines.Num(), ActualLines.Num()));
			return false;
		}
		for (int32 CaseIndex = 0; CaseIndex < ActualLines.Num(); ++CaseIndex)
		{
			FString ExpectedHeader, ExpectedValues, ActualHeader, ActualValues;
			if (!ExpectedLines[CaseIndex].Split(TEXT("|"), &ExpectedHeader, &ExpectedValues)
				|| !ActualLines[CaseIndex].Split(TEXT("|"), &ActualHeader, &ActualValues))
			{
				AddError(FString::Printf(TEXT("invalid capture line %d"), CaseIndex));
				return false;
			}
			TArray<FString> EH, AH;
			ExpectedHeader.ParseIntoArray(EH, TEXT(";"));
			ActualHeader.ParseIntoArray(AH, TEXT(";"));
			if (EH.Num() != 8 || AH.Num() != 8)
			{
				AddError(FString::Printf(TEXT("invalid capture header at case %d"), CaseIndex));
				return false;
			}
			for (int32 Field = 0; Field < 7; ++Field)
			{
				if (EH[Field] != AH[Field])
				{
					AddError(FString::Printf(TEXT("capture case %d metadata mismatch expected=%s actual=%s"), CaseIndex,
						*ExpectedHeader, *ActualHeader));
					return false;
				}
			}
			if (ExpectedValues != ActualValues || EH[7] != AH[7])
			{
				TArray<FString> EV, AV;
				ExpectedValues.ParseIntoArray(EV, TEXT(","));
				ActualValues.ParseIntoArray(AV, TEXT(","));
				if (EV.Num() != AV.Num())
				{
					AddError(FString::Printf(
						TEXT("%s sample count expected=%d actual=%d"), *ActualHeader, EV.Num(), AV.Num()));
					return false;
				}
				for (int32 SampleIndex = 0; SampleIndex < EV.Num(); ++SampleIndex)
				{
					if (EV[SampleIndex] != AV[SampleIndex])
					{
						const int64 X = int64(FCString::Atoi(*AH[3])) * 32 + SampleIndex % 35 - 1;
						const int64 Y = int64(FCString::Atoi(*AH[4])) * 32 + (SampleIndex / 35) % 35 - 1;
						const int64 Z = int64(FCString::Atoi(*AH[5])) * 32 + SampleIndex / (35 * 35) - 1;
						AddError(FString::Printf(
							TEXT(
								"opcode=%s voxel=%s seed=%s key=(%s,%s,%s) fingerprint=%s expected hash=%s actual hash=%s first lattice=(%lld,%lld,%lld) expected=%s actual=%s"),
							*AH[0], *AH[1], *AH[2], *AH[3], *AH[4], *AH[5], *AH[6], *EH[7], *AH[7], X, Y, Z,
							*EV[SampleIndex], *AV[SampleIndex]));
						return false;
					}
				}
				AddError(FString::Printf(
					TEXT("%s matching values but expected hash=%s actual hash=%s"), *ActualHeader, *EH[7], *AH[7]));
				return false;
			}
		}
		AddInfo(FString::Printf(TEXT("All %d cross-host arrays and hashes match %s"), ActualLines.Num(), *ComparePath));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
