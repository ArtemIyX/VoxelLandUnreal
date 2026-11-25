#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldConfig.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Terrain/Shaping/VoxelLandTerrainSmoothingSettings.h"
#include "Data/Terrain/ChunkLocal/VoxelLandChunkLocalTerrainConfig.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Curves/VoxelLandCurveLut.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightGrid.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace
{
constexpr EAutomationTestFlags M4SmoothTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandProgramDraft M4SmoothProgram(bool bX)
{
	FVoxelLandProgramDraft Draft;
	Draft.RegisterCount = 1;
	FVoxelLandInstruction Input, Store;
	Input.Op = bX ? EVoxelLandOpcode::LoadX : EVoxelLandOpcode::LoadZ;
	Store.Op = EVoxelLandOpcode::StoreDensity;
	Draft.Instructions = {Input, Store};
	return Draft;
}

void M4SmoothInitControl(FVoxelLandM4ControlConfig& Control, UObject* Outer)
{
	Control.Graph = NewObject<UVoxelLandTerrainGraph>(Outer);
	FVoxelLandM4CurveAuthoringKey First, Last;
	First.InputQ16 = 0;
	Last.InputQ16 = 65535;
	Control.CurveKeys = {First, Last};
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4SmoothAssetTest, "VoxelLand.M4.Smooth.Asset", M4SmoothTestFlags)
bool FVoxelLandM4SmoothAssetTest::RunTest(const FString& Parameters)
{
	UVoxelLandM4WorldConfig* Config = NewObject<UVoxelLandM4WorldConfig>();
	M4SmoothInitControl(Config->Continental, Config);
	M4SmoothInitControl(Config->Mountain, Config);
	M4SmoothInitControl(Config->Peaks, Config);
	FVoxelLandM4WorldSnapshot Snapshot;
	FString Error;
	TestFalse(TEXT("existing assets default to filter disabled"), Config->bSmoothEnabled);
	if (!TestTrue(TEXT("default configuration captures"), Config->CaptureSnapshot(Snapshot, Error)))
	{
		AddError(Error);
		return false;
	}
	TestFalse(TEXT("snapshot defaults to no filter"), Snapshot.Smooth.bEnabled);
	Config->SmoothStrength = 0.25f;
	TestTrue(TEXT("inactive strength edit does not change density identity"), Config->MatchesSnapshot(Snapshot));
	Config->bSmoothEnabled = true;
	TestFalse(TEXT("filter change supersedes snapshot"), Config->MatchesSnapshot(Snapshot));
	Config->SmoothStrength = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("nonfinite strength rejected"), Config->CaptureSnapshot(Snapshot, Error));
	Config->SmoothStrength = 0.5f;
	Config->SmoothVersion = 2;
	TestFalse(TEXT("unknown version rejected"), Config->CaptureSnapshot(Snapshot, Error));
	Config->SmoothVersion = 1;
	TestTrue(TEXT("enabled filter captures"), Config->CaptureSnapshot(Snapshot, Error));
	TestTrue(TEXT("enabled state captured"), Snapshot.Smooth.bEnabled);
	TestEqual(TEXT("strength captured"), Snapshot.Smooth.Strength, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4SmoothGenerationTest, "VoxelLand.M4.Smooth.Generation", M4SmoothTestFlags)
bool FVoxelLandM4SmoothGenerationTest::RunTest(const FString& Parameters)
{
	bool FiniteChanges = false, UnlimitedChanges = false, DisabledIdentity = false;
	bool HaloGuard = false, InvalidPreserves = false, FingerprintStable = false;
	const bool Completed = Async(EAsyncExecution::ThreadPool, [&]() {
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> FinalProgram, XProgram;
		if (FVoxelLandProgram::Validate(M4SmoothProgram(false), FinalProgram)
			|| FVoxelLandProgram::Validate(M4SmoothProgram(true), XProgram))
		{
			return false;
		}
		const uint64 Fingerprint = FinalProgram->GetFingerprint();
		FVoxelLandGeneratorSettings Final;
		Final.Program = FinalProgram;
		Final.Profile.VoxelSizeCm = 100.f;
		TArray<int32> Heights;
		Heights.Init(0, 81 * 81);
		Heights[40 + 81 * 40] = 25600;
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
		if (!FVoxelLandM4HeightGrid::Create(-4000, -4000, 80, 80, 100, Heights, Grid))
		{
			return false;
		}
		FVoxelLandM4OverhangConfig Overhang;
		FVoxelLandM4SmoothSettings Smooth;
		FVoxelLandChunkKey Key;
		auto Finite = [&](const FVoxelLandM4SmoothSettings& Filter,
						  TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& Chunk) {
			TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
			if (FVoxelLandGenerator::CreateM4HeightField(Final, Grid, false, Overhang, Filter, Generator))
			{
				return false;
			}
			TUniquePtr<FVoxelLandWorkerContext> Worker;
			return !Generator->CreateWorker(Worker) && !Generator->GenerateChunk(Key, *Worker, Chunk);
		};
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Plain, Smoothed, Zero;
		if (!Finite(Smooth, Plain))
		{
			return false;
		}
		Smooth.bEnabled = true;
		Smooth.Strength = 1.f;
		if (!Finite(Smooth, Smoothed))
		{
			return false;
		}
		Smooth.Strength = 0.f;
		if (!Finite(Smooth, Zero))
		{
			return false;
		}
		const int32 Index = 1 + 35 * (1 + 35);
		FiniteChanges =
			Plain && Smoothed && Plain->Density[Index] == -1024 && Smoothed->Density[Index] > Plain->Density[Index];
		DisabledIdentity = Plain && Zero && Plain->Density == Zero->Density;
		TArray<int32> Small;
		Small.Init(0, 35 * 35);
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> SmallGrid;
		if (!FVoxelLandM4HeightGrid::Create(-100, -100, 34, 34, 100, Small, SmallGrid))
		{
			return false;
		}
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Guard;
		Smooth.Strength = 1.f;
		if (FVoxelLandGenerator::CreateM4HeightField(Final, SmallGrid, false, Overhang, Smooth, Guard))
		{
			return false;
		}
		HaloGuard = bool(Guard->ValidateM4ChunkDemand(Key));
		auto Before = Guard;
		Smooth.Version = 2;
		InvalidPreserves = bool(FVoxelLandGenerator::CreateM4HeightField(Final, Grid, false, Overhang, Smooth, Guard))
						&& Guard == Before;
		Smooth.Version = 1;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Controls[3];
		for (int32 I = 0; I < 3; ++I)
		{
			FVoxelLandGeneratorSettings S = Final;
			S.Program = XProgram;
			if (FVoxelLandGenerator::Create(S, Controls[I]))
			{
				return false;
			}
		}
		TArray<int32> Ramp, ZeroValues;
		for (int32 I = 0; I < FVoxelLandM4CurveLut::SampleCount; ++I)
		{
			Ramp.Add(-25600 + I * 51200 / 4096);
			ZeroValues.Add(0);
		}
		TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> RampCurve, ZeroCurve;
		FString CurveError;
		if (!FVoxelLandM4CurveLut::Create(Ramp, -25600, 25600, RampCurve, CurveError)
			|| !FVoxelLandM4CurveLut::Create(ZeroValues, 0, 0, ZeroCurve, CurveError))
		{
			return false;
		}
		FVoxelLandM4UnlimitedConfig Local;
		Local.ContinentalGenerator = Controls[0];
		Local.MountainGenerator = Controls[1];
		Local.PeaksGenerator = Controls[2];
		Local.ContinentalCurve = RampCurve;
		Local.MountainCurve = ZeroCurve;
		Local.PeaksCurve = ZeroCurve;
		auto Unlimited = [&](bool Enabled, TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& Chunk) {
			FVoxelLandM4SmoothSettings Filter;
			Filter.bEnabled = Enabled;
			Filter.Strength = 1.f;
			Local.SmoothOverride = Filter;
			TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
			if (FVoxelLandGenerator::CreateM4Unlimited(Final, Local, Generator))
			{
				return false;
			}
			TUniquePtr<FVoxelLandWorkerContext> Worker;
			return !Generator->CreateWorker(Worker) && !Generator->GenerateChunk(Key, *Worker, Chunk);
		};
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> LocalPlain, LocalSmooth;
		if (!Unlimited(false, LocalPlain) || !Unlimited(true, LocalSmooth))
		{
			return false;
		}
		const int32 Edge = 2 + 35 * (1 + 35);
		UnlimitedChanges = LocalPlain && LocalSmooth && LocalSmooth->Density[Edge] > LocalPlain->Density[Edge];
		FingerprintStable = FinalProgram->GetFingerprint() == Fingerprint;
		return true;
	}).Get();
	TestTrue(TEXT("worker setup and generation completed"), Completed);
	TestTrue(TEXT("authored finite filter changes density"), FiniteChanges);
	TestTrue(TEXT("zero-strength filter is identity"), DisabledIdentity);
	TestTrue(TEXT("filter halo is required for demand"), HaloGuard);
	TestTrue(TEXT("unsupported filter version preserves generator"), InvalidPreserves);
	TestTrue(TEXT("authored unlimited filter changes density"), UnlimitedChanges);
	TestTrue(TEXT("filter settings do not mutate M3 program fingerprint"), FingerprintStable);
	return Completed;
}
#endif
