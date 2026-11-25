#if WITH_DEV_AUTOMATION_TESTS
#include "VoxelLandM6RegionFilterTestFixture.h"
#include "VoxelLandM7CheckpointTestFixture.h"

#include "Async/Async.h"
#include "Data/Structs/VoxelLandM4UnlimitedConfig.h"
#include "Data/Structs/VoxelLandM7NormalFrameBatch.h"
#include "Generation/VoxelLandM4CurveLut.h"
#include "Generation/VoxelLandM7NormalFrames.h"
#include "Misc/AutomationTest.h"

class FVoxelLandM7CompletedFieldFixture final
{
public:
	static FString Pointwise();
	static FString Neighborhood();

private:
	using FFixture = FVoxelLandM7CheckpointTestFixture;
	using FGeneratorPtr = FFixture::FGeneratorPtr;
	using FGridPtr = FFixture::FGridPtr;
	using FChunkPtr = FFixture::FChunkPtr;
	using FFrames = FVoxelLandM7NormalFrames;

	static constexpr uint8 MaxLod = 5;
	static constexpr int32 Cells = FFixture::Cells;
	static constexpr int32 VoxelSizeCm = FFixture::VoxelSizeCm;

	static int32 BaseOwner(int32 BasePoint)
	{
		return BasePoint / Cells - int32(BasePoint % Cells < 0);
	}

	static bool CreateLocal(const FVoxelLandGeneratorSettings& Settings, FGeneratorPtr& Out);
	static bool CheckLevels(const FVoxelLandGenerator& Generator);
	static bool CheckCoincident(const FVoxelLandGenerator& Generator, FVoxelLandWorkerContext& Worker,
		const FVoxelLandChunkData& Coarse, TMap<FIntVector, FChunkPtr>& FineCache);
	static bool CheckNormal(const FVoxelLandGenerator& Generator, FVoxelLandWorkerContext& Worker);
	static bool CheckBudget(FVoxelLandGeneratorSettings Settings, const FGridPtr& Grid, const FChunkPtr& Prior);
	static bool CancelImmediately() { return true; }
};

bool FVoxelLandM7CompletedFieldFixture::CreateLocal(const FVoxelLandGeneratorSettings& Settings, FGeneratorPtr& Out)
{
	FVoxelLandProgramDraft Draft;
	Draft.FormatVersion = 2;
	Draft.RegisterCount = 1;
	FVoxelLandInstruction Load;
	Load.Op = EVoxelLandOpcode::LoadX;
	FVoxelLandInstruction Store;
	Store.Op = EVoxelLandOpcode::StoreDensity;
	Draft.Instructions = {Load, Store};
	FVoxelLandGeneratorSettings ControlSettings;
	FGeneratorPtr Control;
	if (FVoxelLandProgram::Validate(Draft, ControlSettings.Program)
		|| FVoxelLandGenerator::Create(ControlSettings, Control))
	{
		return false;
	}

	constexpr int32 RangeQ8 = 50 * 256;
	constexpr int32 Last = FVoxelLandM4CurveLut::SampleCount - 1;
	TArray<int32> CurveSamples;
	CurveSamples.Reserve(Last + 1);
	for (int32 Index = 0; Index <= Last; ++Index)
	{
		CurveSamples.Add((2 * Index - Last) * RangeQ8 / Last);
	}
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
	FString Error;
	if (!FVoxelLandM4CurveLut::Create(CurveSamples, -RangeQ8, RangeQ8, Curve, Error))
	{
		return false;
	}

	FVoxelLandM4UnlimitedConfig Config;
	Config.ContinentalGenerator = Control;
	Config.MountainGenerator = Control;
	Config.PeaksGenerator = Control;
	Config.ContinentalCurve = Curve;
	Config.MountainCurve = Curve;
	Config.PeaksCurve = Curve;
	Config.bNormalizeSlope = true;
	Config.HeightDetail.bEnabled = true;
	Config.HeightDetail.AmplitudeQ8 = 64 * 256;
	Config.Overhang.Displacement.bEnabled = true;
	Config.Overhang.Displacement.AmplitudeQ8 = 25 * 256;
	Config.SmoothOverride.Emplace();
	Config.SmoothOverride->bEnabled = true;
	Config.SmoothOverride->Strength = 0.5f;
	return !FVoxelLandGenerator::CreateM4Unlimited(Settings, Config, Out);
}

bool FVoxelLandM7CompletedFieldFixture::CheckCoincident(const FVoxelLandGenerator& Generator,
	FVoxelLandWorkerContext& Worker, const FVoxelLandChunkData& Coarse, TMap<FIntVector, FChunkPtr>& FineCache)
{
	const int32 Stride = 1 << Coarse.Key.LOD;
	const FIntVector Offsets[] = {FIntVector(-1, -1, 0), FIntVector::ZeroValue, FIntVector(0, 0, 1),
		FIntVector(1, 2, -1), FIntVector(31, 32, 30), FIntVector(33, 33, 33)};
	if (Coarse.Density.Num() != FVoxelLandChunkData::SampleCount
		|| Coarse.Hash != VoxelLandDeterminism::HashDensity(Coarse.Density))
	{
		return false;
	}
	for (const FIntVector& Local : Offsets)
	{
		const FIntVector Base = (Coarse.Key.Coordinate * Cells + Local) * Stride;
		const FIntVector Owner(BaseOwner(Base.X), BaseOwner(Base.Y), BaseOwner(Base.Z));
		FChunkPtr& Fine = FineCache.FindOrAdd(Owner);
		if (!Fine && (Generator.GenerateChunk({Owner, 0}, Worker, Fine) || !Fine))
		{
			return false;
		}
		if (Coarse.Density[FFixture::Index(Local)] != Fine->Density[FFixture::Index(Base - Owner * Cells)])
		{
			return false;
		}
	}
	return true;
}

bool FVoxelLandM7CompletedFieldFixture::CheckLevels(const FVoxelLandGenerator& Generator)
{
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	if (!Generator.SupportsM7CompletedField() || Generator.CreateWorker(Worker))
	{
		return false;
	}
	TMap<FIntVector, FChunkPtr> FineCache;
	for (uint8 Lod = 1; Lod <= MaxLod; ++Lod)
	{
		const FVoxelLandChunkKey Key{FIntVector::ZeroValue, Lod};
		FChunkPtr Coarse;
		if (Generator.ValidateM4ChunkDemand(Key) || Generator.GenerateChunk(Key, *Worker, Coarse) || !Coarse
			|| !CheckCoincident(Generator, *Worker, *Coarse, FineCache))
		{
			return false;
		}
		if (Lod == MaxLod && (Coarse->Density[FFixture::Index(FIntVector::ZeroValue)] != 2048
			|| Coarse->Density[FFixture::Index(FIntVector(0, 0, 1))] != -2304))
		{
			// A 200 cm clearing at the origin and a 225 cm add sphere at z=3200.
			return false;
		}
	}
	const FVoxelLandChunkKey Negative{FIntVector(-1, 0, 0), MaxLod};
	FChunkPtr Coarse;
	return !Generator.ValidateM4ChunkDemand(Negative) && !Generator.GenerateChunk(Negative, *Worker, Coarse)
		&& Coarse && CheckCoincident(Generator, *Worker, *Coarse, FineCache);
}

FString FVoxelLandM7CompletedFieldFixture::Pointwise()
{
	FVoxelLandGeneratorSettings Settings;
	FGridPtr Grid;
	if (!FFixture::CreateSettings(Settings) || !FFixture::CreateTiltedGrid(-112000, 56, 4000, Grid))
	{
		return TEXT("Finite source or program setup failed");
	}
	FVoxelLandM6Stamp Stamp;
	Stamp.Id = FGuid(0x4d37, 4, 1, 1);
	Stamp.OriginCm = FInt64Vector(0, 0, 3200);
	Stamp.RadiusCm = 225;
	const FVoxelLandM6FilterStage Clear = FVoxelLandM6RegionFilterTestFixture::Stage(
		1, EVoxelLandM6FilterKind::MakeSpace, FInt64Vector(-200), FInt64Vector(200));
	Settings.M6Stamps.Stamps = MakeArrayView(&Stamp, 1);
	Settings.M6Filters.Stages = MakeArrayView(&Clear, 1);
	FGeneratorPtr Finite, Local;
	if (!FFixture::CreateGenerator(Settings, Grid, true, true, Finite) || !CreateLocal(Settings, Local))
	{
		return TEXT("Coarse finite or local source with pointwise M6 setup failed");
	}
	if (!CheckLevels(*Finite))
	{
		return TEXT("Finite LOD1–5 diverged from ordinary LOD0 with pointwise M6");
	}
	if (!CheckLevels(*Local))
	{
		return TEXT("Local LOD1–5 diverged from ordinary LOD0 with pointwise M6");
	}
	return {};
}

bool FVoxelLandM7CompletedFieldFixture::CheckNormal(const FVoxelLandGenerator& Generator,
	FVoxelLandWorkerContext& Worker)
{
	FVoxelLandM7NormalAnchor Anchor;
	Anchor.BaseCell = FInt64Vector(2);
	Anchor.FractionQ16 = FIntVector(32768);
	FChunkPtr Fine;
	FVoxelLandM7NormalFrameBatch Batch;
	if (Generator.GenerateChunk(FVoxelLandChunkKey{}, Worker, Fine) || !Fine
		|| Generator.GenerateM7NormalFrames(MakeArrayView(&Anchor, 1), Worker, Batch, &FFixture::NeverCancel)
		|| Batch.Frames.Num() != 1)
	{
		return false;
	}
	int16 Stencil[FFrames::StencilSamples];
	int32 Index = 0;
	for (int32 Z = -1; Z <= 2; ++Z)
	{
		for (int32 Y = -1; Y <= 2; ++Y)
		{
			for (int32 X = -1; X <= 2; ++X)
			{
				Stencil[Index++] = Fine->Density[FFixture::Index(FIntVector(2 + X, 2 + Y, 2 + Z))];
			}
		}
	}
	FVoxelLandM7NormalFrame Reference;
	return FFrames::Evaluate(MakeArrayView(Stencil), Anchor, Reference)
		&& Batch.Frames[0].Normal == Reference.Normal && Batch.Frames[0].Tangent == Reference.Tangent
		&& Batch.Frames[0].bFallback == Reference.bFallback;
}

bool FVoxelLandM7CompletedFieldFixture::CheckBudget(FVoxelLandGeneratorSettings Settings, const FGridPtr& Grid,
	const FChunkPtr& Prior)
{
	TArray<FVoxelLandM6FilterStage> Filters;
	// The configured halo is legal. Repeated far-level patches exceed per-job work admission.
	for (uint32 Stage = 1; Stage <= 6; ++Stage)
	{
		Filters.Add(FVoxelLandM6RegionFilterTestFixture::Stage(
			Stage, EVoxelLandM6FilterKind::Smooth, FInt64Vector(-110000), FInt64Vector(110000)));
	}
	Settings.M6Filters.Stages = Filters;
	FGeneratorPtr Budgeted;
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	if (!FFixture::CreateGenerator(Settings, Grid, false, false, Budgeted) || Budgeted->CreateWorker(Worker))
	{
		return false;
	}
	const FVoxelLandChunkKey Key{FIntVector::ZeroValue, MaxLod};
	FChunkPtr Out = Prior;
	const FVoxelLandError Error = Budgeted->GenerateChunk(Key, *Worker, Out);
	return Error.Code == EVoxelLandErrorCode::InvalidParameter && Error.Message.Contains(TEXT("work budget"))
		&& Out == Prior;
}

FString FVoxelLandM7CompletedFieldFixture::Neighborhood()
{
	FVoxelLandGeneratorSettings Settings;
	FGridPtr Grid;
	if (!FFixture::CreateSettings(Settings) || !FFixture::CreateTiltedGrid(-112000, 56, 4000, Grid))
	{
		return TEXT("Neighborhood source or program setup failed");
	}
	FVoxelLandM6Stamp Stamp;
	Stamp.Id = FGuid(0x4d37, 4, 2, 1);
	Stamp.OriginCm = FInt64Vector(200);
	Stamp.RadiusCm = 190;
	const FVoxelLandM6FilterStage Smooth = FVoxelLandM6RegionFilterTestFixture::Stage(
		1, EVoxelLandM6FilterKind::Smooth, FInt64Vector(-1000), FInt64Vector(1000));
	Settings.M6Stamps.Stamps = MakeArrayView(&Stamp, 1);
	Settings.M6Filters.Stages = MakeArrayView(&Smooth, 1);
	Settings.M6Redistance.bEnabled = true;
	Settings.M6Redistance.BandVoxels = 1;

	FGeneratorPtr Generator;
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	if (!FFixture::CreateGenerator(Settings, Grid, false, false, Generator)
		|| Generator->CreateWorker(Worker) || Generator->GetM6RedistanceIdentity().IsEmpty())
	{
		return TEXT("Finite M6 neighborhood setup failed");
	}

	// Stride eight and band one use single-output patches away from the smooth mask.
	const FVoxelLandChunkKey Key{FIntVector::ZeroValue, 3};
	FChunkPtr Coarse;
	TMap<FIntVector, FChunkPtr> FineCache;
	if (Generator->GenerateChunk(Key, *Worker, Coarse) || !Coarse
		|| !CheckCoincident(*Generator, *Worker, *Coarse, FineCache) || !CheckNormal(*Generator, *Worker))
	{
		return TEXT("Generic re-distance tile or normal stencil diverged from the LOD0 completed field");
	}
	FChunkPtr Cancelled = Coarse;
	if (Generator->GenerateChunkCancellable(Key, *Worker, Cancelled, &CancelImmediately).Code
			!= EVoxelLandErrorCode::Cancelled || Cancelled != Coarse || !CheckBudget(Settings, Grid, Coarse))
	{
		return TEXT("Cancellation or coarse work rejection changed the published output");
	}
	return {};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM7CompletedFieldPointwiseTest, "VoxelLand.M7.Field.PointwiseAcrossLevels",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM7CompletedFieldPointwiseTest::RunTest(const FString& Parameters)
{
	const FString Error = Async(EAsyncExecution::ThreadPool, &FVoxelLandM7CompletedFieldFixture::Pointwise).Get();
	return TestTrue(Error.IsEmpty() ? TEXT("Finite and local coarse fields reproduce coincident LOD0 samples") : Error,
		Error.IsEmpty());
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM7CompletedFieldNeighborhoodTest, "VoxelLand.M7.Field.NeighborhoodPatches",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM7CompletedFieldNeighborhoodTest::RunTest(const FString& Parameters)
{
	const FString Error = Async(EAsyncExecution::ThreadPool, &FVoxelLandM7CompletedFieldFixture::Neighborhood).Get();
	return TestTrue(Error.IsEmpty() ? TEXT("Coarse M6 patches and normal stencils retain completed-field semantics") : Error,
		Error.IsEmpty());
}
#endif
