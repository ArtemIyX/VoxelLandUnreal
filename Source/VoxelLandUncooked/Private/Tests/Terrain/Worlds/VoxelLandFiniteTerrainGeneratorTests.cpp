#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightGrid.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M4GeneratorFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandProgramDraft M4GeneratorProgram(bool bSmooth)
{
	FVoxelLandProgramDraft Draft;
	Draft.FormatVersion = 2;
	Draft.RegisterCount = 1;
	FVoxelLandInstruction Load;
	Load.Op = EVoxelLandOpcode::LoadZ;
	FVoxelLandInstruction Store;
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

bool M4GeneratorOverlap(const FVoxelLandChunkData& Left, const FVoxelLandChunkData& Right, int32 Axis)
{
	constexpr int32 N = FVoxelLandChunkData::SamplesPerAxis;
	for (int32 A = 0; A < N; ++A)
	{
		for (int32 B = 0; B < N; ++B)
		{
			for (int32 Plane = 0; Plane < 3; ++Plane)
			{
				int32 L[3] = {A, B, Plane + 32};
				int32 R[3] = {A, B, Plane};
				if (Axis == 0)
				{
					L[0] = Plane + 32;
					R[0] = Plane;
					L[1] = R[1] = A;
					L[2] = R[2] = B;
				}
				if (Axis == 1)
				{
					L[0] = R[0] = A;
					L[1] = Plane + 32;
					R[1] = Plane;
					L[2] = R[2] = B;
				}
				const int32 LI = L[0] + N * (L[1] + N * L[2]);
				const int32 RI = R[0] + N * (R[1] + N * R[2]);
				if (Left.Density[LI] != Right.Density[RI])
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
	FVoxelLandM4GeneratorIntegrationTest, "VoxelLand.M4.Generator.HeightField", M4GeneratorFlags)
bool FVoxelLandM4GeneratorIntegrationTest::RunTest(const FString& Parameters)
{
	bool bOverlap[2][3] = {};
	bool bFiniteReject = false, bCancelled = false, bLegacy = false, bUnknownBounds = false, bRawReject = false;
	bool bSlopeMargin = false, bFilterMargin = false, bCorrectBase = true, bSlopeValue = false;
	bool bDemandAccepted = false, bDemandRejected = false, bDemandLOD = false, bLegacyDemand = false;
	const bool bCompleted = Async(EAsyncExecution::ThreadPool, [&]() {
		TArray<int32> Heights;
		Heights.Init(0, 129 * 129);
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
		if (!FVoxelLandM4HeightGrid::Create(-6400, -6400, 128, 128, 100, Heights, Grid))
		{
			return false;
		}
		for (int32 Smooth = 0; Smooth < 2; ++Smooth)
		{
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
			if (FVoxelLandProgram::Validate(M4GeneratorProgram(Smooth != 0), Program))
			{
				return false;
			}
			FVoxelLandGeneratorSettings Settings;
			Settings.Program = Program;
			Settings.Profile.VoxelSizeCm = 100.f;
			TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
			if (FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, true, Generator))
			{
				return false;
			}
			TUniquePtr<FVoxelLandWorkerContext> Worker;
			if (Generator->CreateWorker(Worker))
			{
				return false;
			}
			FVoxelLandChunkKey Key;
			Key.Coordinate = FIntVector(-1, -1, -1);
			bDemandAccepted = !Generator->ValidateM4ChunkDemand(Key);
			FVoxelLandChunkKey WrongLOD = Key;
			WrongLOD.LOD = 6; // M7 admits levels 0–5, subject to the full field footprint.
			bDemandLOD = Generator->ValidateM4ChunkDemand(WrongLOD).Code == EVoxelLandErrorCode::UnsupportedLod;
			bUnknownBounds = Generator->ClassifyChunkBeforeSampling(Key) == EVoxelLandChunkClassification::NeedsSamples;
			TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Base;
			if (Generator->GenerateChunk(Key, *Worker, Base) || !Base
				|| Base->Density.Num() != FVoxelLandChunkData::SampleCount)
			{
				return false;
			}
			bCorrectBase &= Base->Density[32 + 35 * (32 + 35 * 32)] == -1024;
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
				if (Generator->GenerateChunk(Neighbor, *Worker, Adjacent) || !Adjacent)
				{
					return false;
				}
				bOverlap[Smooth][Axis] = M4GeneratorOverlap(*Base, *Adjacent, Axis);
			}
			FVoxelLandChunkKey Outside = Key;
			Outside.Coordinate.X = -2;
			bDemandRejected = bool(Generator->ValidateM4ChunkDemand(Outside));
			TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Existing = Base;
			const FVoxelLandError OutsideError = Generator->GenerateChunk(Outside, *Worker, Existing);
			bFiniteReject = bool(OutsideError) && Existing == Base;
			int32 Checks = 0;
			Existing = Base;
			const FVoxelLandError Cancelled =
				Generator->GenerateChunkCancellable(Key, *Worker, Existing, [&Checks] { return ++Checks > 4; });
			bCancelled = Cancelled.Code == EVoxelLandErrorCode::Cancelled && Existing == Base;
			TArray<float> Raw;
			const FVector3f P(0.f, 0.f, 0.f);
			bRawReject = bool(Generator->EvaluateSamplesRaw(MakeArrayView(&P, 1), *Worker, Raw));
			// The stored footprint alone reaches -3300 cm. A slope stencil needs
			// -3400; an active one-cell filter needs another 100 cm beyond that.
			const int32 LimitedCells = Smooth ? 68 : 66;
			const int32 LimitedOrigin = Smooth ? -3400 : -3300;
			TArray<int32> LimitedHeights;
			LimitedHeights.Init(0, (LimitedCells + 1) * (LimitedCells + 1));
			TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> LimitedGrid;
			if (!FVoxelLandM4HeightGrid::Create(
					LimitedOrigin, LimitedOrigin, LimitedCells, LimitedCells, 100, LimitedHeights, LimitedGrid))
			{
				return false;
			}
			TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> LimitedGenerator;
			if (FVoxelLandGenerator::CreateM4HeightField(Settings, LimitedGrid, true, LimitedGenerator))
			{
				return false;
			}
			TUniquePtr<FVoxelLandWorkerContext> LimitedWorker;
			if (LimitedGenerator->CreateWorker(LimitedWorker))
			{
				return false;
			}
			Existing = Base;
			const bool bGuarded =
				bool(LimitedGenerator->GenerateChunk(Key, *LimitedWorker, Existing)) && Existing == Base;
			const bool bDemandGuarded = bool(LimitedGenerator->ValidateM4ChunkDemand(Key));
			if (Smooth)
			{
				bFilterMargin = bGuarded && bDemandGuarded;
			}
			else
			{
				bSlopeMargin = bGuarded && bDemandGuarded;
			}
			if (!Smooth)
			{
				TArray<int32> SlopedHeights;
				SlopedHeights.Reserve(129 * 129);
				for (int32 Y = 0; Y < 129; ++Y)
				{
					for (int32 X = 0; X < 129; ++X)
					{
						SlopedHeights.Add((-6400 + X * 100) * 128);
					}
				}
				TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> SlopedGrid;
				if (!FVoxelLandM4HeightGrid::Create(-6400, -6400, 128, 128, 100, SlopedHeights, SlopedGrid))
				{
					return false;
				}
				TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> SlopedGenerator;
				if (FVoxelLandGenerator::CreateM4HeightField(Settings, SlopedGrid, true, SlopedGenerator))
				{
					return false;
				}
				TUniquePtr<FVoxelLandWorkerContext> SlopedWorker;
				if (SlopedGenerator->CreateWorker(SlopedWorker))
				{
					return false;
				}
				FVoxelLandChunkKey Origin;
				TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> SlopedChunk;
				if (SlopedGenerator->GenerateChunk(Origin, *SlopedWorker, SlopedChunk) || !SlopedChunk)
				{
					return false;
				}
				// At x=0,z=100 the exact normalized density is 1024/sqrt(1.25) voxels/1024.
				bSlopeValue = SlopedChunk->Density[1 + 35 * (1 + 35 * 2)] == 916;
				TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Old;
				if (FVoxelLandGenerator::Create(Settings, Old))
				{
					return false;
				}
				bLegacyDemand = !Old->ValidateM4ChunkDemand(Outside);
				TUniquePtr<FVoxelLandWorkerContext> OldWorker;
				if (Old->CreateWorker(OldWorker))
				{
					return false;
				}
				TArray<float> OldRaw;
				const FVector3f OldPosition(0.f, 0.f, 123.f);
				bLegacy = !Old->EvaluateSamplesRaw(MakeArrayView(&OldPosition, 1), *OldWorker, OldRaw)
					   && OldRaw.Num() == 1 && OldRaw[0] == 123.f;
			}
		}
		return true;
	}).Get();
	TestTrue(TEXT("height-field work completed"), bCompleted);
	for (int32 Smooth = 0; Smooth < 2; ++Smooth)
	{
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			TestTrue(FString::Printf(TEXT("negative-coordinate overlap axis %d smooth %d"), Axis, Smooth),
				bOverlap[Smooth][Axis]);
		}
	}
	TestTrue(TEXT("unproven classification remains unknown"), bUnknownBounds);
	TestTrue(TEXT("valid M4 demand accepted"), bDemandAccepted);
	TestTrue(TEXT("out-of-domain M4 demand rejected"), bDemandRejected);
	TestTrue(TEXT("unsupported M4 demand LOD rejected"), bDemandLOD);
	TestTrue(TEXT("M3 demand retains existing handling"), bLegacyDemand);
	TestTrue(TEXT("flat field yields expected quantized density"), bCorrectBase);
	TestTrue(TEXT("slope normalization feeds quantized field"), bSlopeValue);
	TestTrue(TEXT("slope stencil must fit grid"), bSlopeMargin);
	TestTrue(TEXT("smooth halo plus slope stencil must fit grid"), bFilterMargin);
	TestTrue(TEXT("grid and halo failure preserve output"), bFiniteReject);
	TestTrue(TEXT("cancellation preserves output"), bCancelled);
	TestTrue(TEXT("raw float sample API rejects height field"), bRawReject);
	TestTrue(TEXT("ordinary M3 VM retains its behavior"), bLegacy);
	return bCompleted;
}

#endif
