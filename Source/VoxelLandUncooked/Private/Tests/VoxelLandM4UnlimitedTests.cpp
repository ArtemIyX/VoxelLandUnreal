#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4CurveLut.h"
#include "Generation/VoxelLandM4Unlimited.h"
#include "Data/Structs/VoxelLandM4UnlimitedConfig.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M4UnlimitedFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandProgramDraft M4UnlimitedControl(bool bX, float Constant)
{
	FVoxelLandProgramDraft Draft;
	Draft.RegisterCount = bX ? 3 : 1;
	Draft.Constants = bX ? TArray<float>({Constant, 1.f, 0.5f, 2.f}) : TArray<float>({Constant});
	FVoxelLandInstruction Input, Load, NoiseOp, Store;
	Input.Op = bX ? EVoxelLandOpcode::LoadX : EVoxelLandOpcode::LoadConstant;
	Load.Op = EVoxelLandOpcode::LoadY;
	Load.Dst = 1;
	NoiseOp.Op = EVoxelLandOpcode::Value2;
	NoiseOp.A = 0;
	NoiseOp.B = 1;
	NoiseOp.Dst = 2;
	if (bX)
	{
		FVoxelLandSeedSlot Seed;
		Seed.NodeId = FGuid(1, 2, 3, 4);
		Draft.Seeds.Add(Seed);
		FVoxelLandNoiseConfig Noise;
		Noise.Frequency.Index = 0;
		Noise.Octaves.Index = 1;
		Noise.Gain.Index = 2;
		Noise.Lacunarity.Index = 3;
		Draft.Noise.Add(Noise);
	}
	Store.Op = EVoxelLandOpcode::StoreDensity;
	Store.A = bX ? 2 : 0;
	Draft.Instructions = bX ? TArray<FVoxelLandInstruction>({Input, Load, NoiseOp, Store})
							: TArray<FVoxelLandInstruction>({Input, Store});
	return Draft;
}

FVoxelLandProgramDraft M4UnlimitedFinal(bool bSmooth)
{
	FVoxelLandProgramDraft Draft;
	Draft.FormatVersion = 2;
	Draft.RegisterCount = 1;
	FVoxelLandInstruction Load, Store;
	Load.Op = EVoxelLandOpcode::LoadZ;
	Store.Op = EVoxelLandOpcode::StoreDensity;
	Draft.Instructions = {Load, Store};
	if (bSmooth)
	{
		Draft.Constants = {1.f, 0.5f};
		Draft.SmoothFilter.bPresent = true;
		Draft.SmoothFilter.Enabled.Index = 0;
		Draft.SmoothFilter.Strength.Index = 1;
	}
	return Draft;
}

bool M4UnlimitedOverlap(const FVoxelLandChunkData& Left, const FVoxelLandChunkData& Right, int32 Axis)
{
	constexpr int32 N = FVoxelLandChunkData::SamplesPerAxis;
	for (int32 A = 0; A < N; ++A)
	{
		for (int32 B = 0; B < N; ++B)
		{
			for (int32 P = 0; P < 3; ++P)
			{
				int32 L[3] = {A, B, P + 32}, R[3] = {A, B, P};
				if (Axis == 0)
				{
					L[0] = P + 32;
					R[0] = P;
					L[1] = R[1] = A;
					L[2] = R[2] = B;
				}
				if (Axis == 1)
				{
					L[0] = R[0] = A;
					L[1] = P + 32;
					R[1] = P;
					L[2] = R[2] = B;
				}
				if (Left.Density[L[0] + N * (L[1] + N * L[2])] != Right.Density[R[0] + N * (R[1] + N * R[2])])
				{
					return false;
				}
			}
		}
	}
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4UnlimitedChunkTest, "VoxelLand.M4.Unlimited.Chunk", M4UnlimitedFlags)
bool FVoxelLandM4UnlimitedChunkTest::RunTest(const FString& Parameters)
{
	bool Overlap[2][2][3] = {};
	bool OrderIndependent = true, CancelledPreserved = false, PrecisionRejected = false, NonFlat = false;
	bool Integrated = true, DemandValid = true, WrongWorkerRejected = true, ExplicitFilterRejected = true;
	bool UnsafeControlRejected = true, IntegratedRangeRejected = true, WrongThreadRejected = true;
	const bool Completed = Async(EAsyncExecution::ThreadPool, [&]() {
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Controls[3];
		TUniquePtr<FVoxelLandWorkerContext> Workers[3];
		for (int32 I = 0; I < 3; ++I)
		{
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
			if (FVoxelLandProgram::Validate(M4UnlimitedControl(I == 0, I == 0 ? 0.0001f : float(I) * 0.2f), Program))
			{
				return false;
			}
			FVoxelLandGeneratorSettings Settings;
			Settings.Program = Program;
			Settings.WorldSeed = 7 + I;
			if (FVoxelLandGenerator::Create(Settings, Controls[I]) || Controls[I]->CreateWorker(Workers[I]))
			{
				return false;
			}
		}
		TArray<int32> CurveValues;
		for (int32 I = 0; I < FVoxelLandM4CurveLut::SampleCount; ++I)
		{
			CurveValues.Add(I * 16);
		}
		TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
		FString CurveError;
		if (!FVoxelLandM4CurveLut::Create(CurveValues, 0, 65536, Curve, CurveError))
		{
			return false;
		}
		FVoxelLandChunkKey Key;
		Key.Coordinate = FIntVector(-1, -1, -1);
		for (int32 Smooth = 0; Smooth < 2; ++Smooth)
		{
			for (int32 Slope = 0; Slope < 2; ++Slope)
			{
				FVoxelLandGeneratorSettings Final;
				if (FVoxelLandProgram::Validate(M4UnlimitedFinal(Smooth != 0), Final.Program))
				{
					return false;
				}
				Final.Profile.VoxelSizeCm = 100.f;
				FVoxelLandM4UnlimitedConfig Config;
				Config.ContinentalGenerator = Controls[0];
				Config.MountainGenerator = Controls[1];
				Config.PeaksGenerator = Controls[2];
				Config.ContinentalCurve = Curve;
				Config.MountainCurve = Curve;
				Config.PeaksCurve = Curve;
				Config.bNormalizeSlope = Slope != 0;
				auto Generate = [&](const FVoxelLandChunkKey& K,
									TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& Out,
									TFunctionRef<bool()> Cancel) {
					return VoxelLandM4Unlimited::GenerateChunk(Final, Config, K, Out, Cancel);
				};
				TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Base, Again;
				if (Generate(Key, Base, [] { return false; }) || !Base)
				{
					return false;
				}
				TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> LocalGenerator;
				if (FVoxelLandGenerator::CreateM4Unlimited(Final, Config, LocalGenerator) || !LocalGenerator)
				{
					return false;
				}
				TUniquePtr<FVoxelLandWorkerContext> LocalWorker;
				if (LocalGenerator->CreateWorker(LocalWorker) || !LocalWorker)
				{
					return false;
				}
				TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> IntegratedChunk;
				if (LocalGenerator->GenerateChunk(Key, *LocalWorker, IntegratedChunk) || !IntegratedChunk)
				{
					return false;
				}
				Integrated &= Base->Hash == IntegratedChunk->Hash && Base->Density == IntegratedChunk->Density;
				DemandValid &=
					!LocalGenerator->ValidateM4ChunkDemand(Key)
					&& LocalGenerator->ClassifyChunkBeforeSampling(Key) == EVoxelLandChunkClassification::NeedsSamples;
				FVoxelLandM4UnlimitedConfig Nested = Config;
				Nested.ContinentalGenerator = LocalGenerator;
				TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Rejected = LocalGenerator;
				UnsafeControlRejected &=
					bool(FVoxelLandGenerator::CreateM4Unlimited(Final, Nested, Rejected)) && Rejected == LocalGenerator;
				TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> PriorIntegrated = IntegratedChunk;
				WrongWorkerRejected &= LocalGenerator->GenerateChunk(Key, *Workers[0], PriorIntegrated).Code
										== EVoxelLandErrorCode::InvalidContext
									&& PriorIntegrated == IntegratedChunk;
				if (!Smooth && !Slope)
				{
					// A dedicated thread cannot match the scheduler worker's owner.
					WrongThreadRejected =
						Async(EAsyncExecution::Thread, [&LocalGenerator, &LocalWorker, &Key, &IntegratedChunk]() {
							TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Prior = IntegratedChunk;
							return LocalGenerator->GenerateChunk(Key, *LocalWorker, Prior).Code
									== EVoxelLandErrorCode::InvalidContext
								&& Prior == IntegratedChunk;
						}).Get();
				}
				ExplicitFilterRejected &= LocalGenerator
												  ->GenerateSmoothedChunkCancellable(Key, *LocalWorker, true, 0.5f,
													  PriorIntegrated, [] { return false; })
												  .Code
										   == EVoxelLandErrorCode::InvalidContext
									   && PriorIntegrated == IntegratedChunk;
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					FVoxelLandChunkKey Neighbor = Key;
					if (Axis == 0)
					{
						++Neighbor.Coordinate.X;
					}
					if (Axis == 1)
					{
						++Neighbor.Coordinate.Y;
					}
					if (Axis == 2)
					{
						++Neighbor.Coordinate.Z;
					}
					TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Adjacent;
					if (Generate(Neighbor, Adjacent, [] { return false; }) || !Adjacent)
					{
						return false;
					}
					Overlap[Smooth][Slope][Axis] = M4UnlimitedOverlap(*Base, *Adjacent, Axis);
				}
				if (Generate(Key, Again, [] { return false; }) || !Again)
				{
					return false;
				}
				OrderIndependent &= Base->Hash == Again->Hash && Base->Density == Again->Density;
				NonFlat |= Base->Density[1 + 35 * (1 + 35 * 33)] != Base->Density[33 + 35 * (1 + 35 * 33)];
				int32 Checks = 0;
				TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Prior = Base;
				const FVoxelLandError CancelError = Generate(Key, Prior, [&Checks] { return ++Checks >= 4; });
				CancelledPreserved |= CancelError.Code == EVoxelLandErrorCode::Cancelled && Prior == Base;
				FVoxelLandChunkKey Far = Key;
				Far.Coordinate.X = MAX_int32;
				IntegratedRangeRejected &=
					LocalGenerator->ValidateM4ChunkDemand(Far).Code == EVoxelLandErrorCode::NonFiniteCoordinate;
				Prior = Base;
				const FVoxelLandError RangeError = Generate(Far, Prior, [] { return false; });
				PrecisionRejected |= RangeError.Code == EVoxelLandErrorCode::NonFiniteCoordinate && Prior == Base;
			}
		}
		return true;
	}).Get();
	TestTrue(TEXT("local terrain evaluation completed"), Completed);
	for (int32 Smooth = 0; Smooth < 2; ++Smooth)
	{
		for (int32 Slope = 0; Slope < 2; ++Slope)
		{
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				TestTrue(FString::Printf(
							 TEXT("shared negative-coordinate plane, smooth %d slope %d axis %d"), Smooth, Slope, Axis),
					Overlap[Smooth][Slope][Axis]);
			}
		}
	}
	TestTrue(TEXT("same global chunk matches after neighbors"), OrderIndependent);
	TestTrue(TEXT("control output affects height spatially"), NonFlat);
	TestTrue(TEXT("scheduler generator matches standalone local terrain"), Integrated);
	TestTrue(TEXT("valid local demand and conservative classification"), DemandValid);
	TestTrue(TEXT("worker from different generator rejected"), WrongWorkerRejected);
	TestTrue(TEXT("worker from another thread rejected"), WrongThreadRejected);
	TestTrue(TEXT("unconfigured direct filter rejected without publication"), ExplicitFilterRejected);
	TestTrue(TEXT("nested local generator rejected as a control"), UnsafeControlRejected);
	TestTrue(TEXT("scheduler gate rejects unrepresentable local terrain"), IntegratedRangeRejected);
	TestTrue(TEXT("cancellation preserves output"), CancelledPreserved);
	TestTrue(TEXT("unrepresentable coordinate rejected"), PrecisionRejected);
	return Completed;
}

#endif
