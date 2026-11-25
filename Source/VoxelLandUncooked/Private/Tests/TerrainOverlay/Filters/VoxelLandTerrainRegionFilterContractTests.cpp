#if WITH_DEV_AUTOMATION_TESTS
#include "VoxelLandTerrainRegionFilterTestFixture.h"

#include "Async/Async.h"
#include "Misc/AutomationTest.h"
#include <functional>

class FVoxelLandM6RegionContractFixture final
{
public:
	static FString Run();

private:
	using FBase = FVoxelLandM6RegionFilterTestFixture;
	using FFixture = FBase::FFixture;
	using FChunkPtr = FBase::FChunkPtr;
	using FGeneratorPtr = FBase::FGeneratorPtr;
	using FGridPtr = FBase::FGridPtr;

	static bool CheckOperations(FVoxelLandGeneratorSettings Settings, const FGridPtr& Grid);
	static bool CheckFootprints(FVoxelLandGeneratorSettings Settings);
	static bool CheckCancellation(const FVoxelLandGenerator& Generator);
	static bool CountPoll(int32& Polls)
	{
		++Polls;
		return false;
	}
	static bool CancelOnZero(int32& Remaining) { return --Remaining == 0; }
};

bool FVoxelLandM6RegionContractFixture::CheckOperations(FVoxelLandGeneratorSettings Settings, const FGridPtr& Grid)
{
	const FVoxelLandM6FilterStage Clear =
		FBase::Stage(1, EVoxelLandM6FilterKind::MakeSpace, FInt64Vector(-51, -50, -90), FInt64Vector(50, 50, 99));
	const FVoxelLandM6FilterStage Smooth =
		FBase::Stage(2, EVoxelLandM6FilterKind::Smooth, FInt64Vector(0), FInt64Vector(0));
	const FVoxelLandM6FilterStage Flatten =
		FBase::Stage(3, EVoxelLandM6FilterKind::Flatten, FInt64Vector(0, 0, 0), FInt64Vector(0, 0, 400));
	const FVoxelLandM6FilterStage SmoothAfter[] = {Clear, Smooth};
	const FVoxelLandM6FilterStage ClearAfter[] = {Smooth, Clear};
	const FVoxelLandM6FilterStage FlattenAfter[] = {Clear, Flatten};
	FGeneratorPtr Baseline, SmoothGenerator, ClearGenerator, FlattenGenerator;
	if (!FBase::CreateFinite(Settings, Grid, {}, false, Baseline)
		|| !FBase::CreateFinite(Settings, Grid, SmoothAfter, false, SmoothGenerator)
		|| !FBase::CreateFinite(Settings, Grid, ClearAfter, false, ClearGenerator)
		|| !FBase::CreateFinite(Settings, Grid, FlattenAfter, false, FlattenGenerator))
	{
		return false;
	}
	FChunkPtr Original, Smoothed, Cleared, Flattened;
	const FVoxelLandChunkKey Key;
	if (!FBase::Generate(*Baseline, Key, Original) || !FBase::Generate(*SmoothGenerator, Key, Smoothed)
		|| !FBase::Generate(*ClearGenerator, Key, Cleared) || !FBase::Generate(*FlattenGenerator, Key, Flattened))
	{
		return false;
	}

	const int32 Center = FFixture::SampleIndex(FIntVector(0), FFixture::Samples);
	const int32 Outside = FFixture::SampleIndex(FIntVector(0, 1, 0), FFixture::Samples);
	const int32 NoCrossing = FFixture::SampleIndex(FIntVector(0, 0, 4), FFixture::Samples);
	const TConstArrayView<uint8> SmoothBytes = SmoothGenerator->GetM6FilterIdentity();
	const TConstArrayView<uint8> ClearBytes = ClearGenerator->GetM6FilterIdentity();
	// The plane sits half a voxel above the center. Clearing changes that
	// sample from -512 to +512; the new 27-sample mean is -12800/27.
	// Flatten sees eight rising crossings at +512 and the cleared one at -256.
	return Baseline->GetM6FilterIdentity().IsEmpty() && SmoothGenerator->GetM6StampIdentity().IsEmpty()
		&& SmoothBytes.Num() > 0 && SmoothBytes.Num() == ClearBytes.Num()
		&& FMemory::Memcmp(SmoothBytes.GetData(), ClearBytes.GetData(), SmoothBytes.Num()) != 0
		&& Original->Density[Center] == -512 && Smoothed->Density[Center] == -474 && Cleared->Density[Center] == 512
		&& Flattened->Density[Center] == -427 && Smoothed->Density[Outside] == Original->Density[Outside]
		&& Flattened->Density[NoCrossing] == Original->Density[NoCrossing] && CheckCancellation(*SmoothGenerator);
}

bool FVoxelLandM6RegionContractFixture::CheckFootprints(FVoxelLandGeneratorSettings Settings)
{
	TArray<int32> Heights;
	Heights.Init(50 * 256, 35 * 35);
	FGridPtr Narrow, OneHalo;
	if (!FVoxelLandM4HeightGrid::Create(-100, -100, 34, 34, 100, Heights, Narrow))
	{
		return false;
	}
	Heights.Init(50 * 256, 37 * 37);
	if (!FVoxelLandM4HeightGrid::Create(-200, -200, 36, 36, 100, Heights, OneHalo))
	{
		return false;
	}

	const FVoxelLandM6FilterStage Far =
		FBase::Stage(1, EVoxelLandM6FilterKind::Smooth, FInt64Vector(5000), FInt64Vector(6000));
	const FVoxelLandM6FilterStage NoLatticeSample =
		FBase::Stage(2, EVoxelLandM6FilterKind::Smooth, FInt64Vector(1, -100, -100), FInt64Vector(99, 100, 100));
	const FVoxelLandM6FilterStage Early =
		FBase::Stage(3, EVoxelLandM6FilterKind::Smooth, FInt64Vector(3400, 0, 0), FInt64Vector(3400, 0, 0));
	const FVoxelLandM6FilterStage Later =
		FBase::Stage(4, EVoxelLandM6FilterKind::Smooth, FInt64Vector(3300, 0, 0), FInt64Vector(3300, 0, 0));
	const FVoxelLandM6FilterStage Zero =
		FBase::Stage(5, EVoxelLandM6FilterKind::Smooth, FInt64Vector(0), FInt64Vector(0), 0);
	const FVoxelLandM6FilterStage Inactive[] = {Far, NoLatticeSample, Zero};
	const FVoxelLandM6FilterStage Coupled[] = {Early, Later};
	FGeneratorPtr Baseline, Skipped, OneStage, TwoStages, Disabled;
	if (!FBase::CreateFinite(Settings, Narrow, {}, false, Baseline)
		|| !FBase::CreateFinite(Settings, Narrow, Inactive, false, Skipped)
		|| !FBase::CreateFinite(Settings, OneHalo, MakeArrayView(&Later, 1), false, OneStage)
		|| !FBase::CreateFinite(Settings, OneHalo, Coupled, false, TwoStages))
	{
		return false;
	}
	Settings.M6Filters.bEnabled = false;
	if (!FBase::CreateFinite(Settings, Narrow, Coupled, false, Disabled) || !Disabled->GetM6FilterIdentity().IsEmpty())
	{
		return false;
	}

	const FVoxelLandChunkKey Key;
	FChunkPtr Original, SkippedChunk, DisabledChunk;
	if (Skipped->ValidateM4ChunkDemand(Key) || OneStage->ValidateM4ChunkDemand(Key)
		|| !TwoStages->ValidateM4ChunkDemand(Key) || !FBase::Generate(*Baseline, Key, Original)
		|| !FBase::Generate(*Skipped, Key, SkippedChunk) || !FBase::Generate(*Disabled, Key, DisabledChunk)
		|| Original->Hash != SkippedChunk->Hash || Original->Density != SkippedChunk->Density
		|| Original->Hash != DisabledChunk->Hash || Original->Density != DisabledChunk->Density)
	{
		return false;
	}

	TUniquePtr<FVoxelLandWorkerContext> Worker;
	FChunkPtr Protected = Original;
	if (TwoStages->CreateWorker(Worker) || !TwoStages->GenerateChunk(Key, *Worker, Protected) || Protected != Original)
	{
		return false;
	}
	Settings.M6Filters.bEnabled = true;
	TArray<FVoxelLandM6FilterStage> TooMany;
	TooMany.Init(Later, FVoxelLandM6FilterSettings::MaxStages + 1);
	FGeneratorPtr Prior = Baseline;
	return !FBase::CreateFinite(Settings, OneHalo, TooMany, false, Prior) && Prior == Baseline;
}

bool FVoxelLandM6RegionContractFixture::CheckCancellation(const FVoxelLandGenerator& Generator)
{
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	if (Generator.CreateWorker(Worker))
	{
		return false;
	}
	const FVoxelLandChunkKey Key;
	int32 Polls = 0;
	FChunkPtr First;
	if (Generator.GenerateChunkCancellable(Key, *Worker, First, std::bind_front(&CountPoll, std::ref(Polls))))
	{
		return false;
	}
	int32 Remaining = Polls - 2; // The final two checks belong to publication, after the last filter row.
	FChunkPtr Protected = First;
	const FVoxelLandError Error = Generator.GenerateChunkCancellable(
		Key, *Worker, Protected, std::bind_front(&CancelOnZero, std::ref(Remaining)));
	return Error.Code == EVoxelLandErrorCode::Cancelled && Error.Message.Contains(TEXT("region filter"))
		&& Protected == First;
}

FString FVoxelLandM6RegionContractFixture::Run()
{
	FVoxelLandGeneratorSettings Settings;
	FGridPtr Grid;
	if (!FFixture::CreateInputs(50, Settings, Grid))
	{
		return TEXT("Flat half-voxel M4 reference setup failed");
	}
	if (!CheckOperations(Settings, Grid))
	{
		return TEXT("Region order, smooth/flatten reference, identity or cancellation differs");
	}
	if (!CheckFootprints(Settings))
	{
		return TEXT("Backward lattice footprint, compatibility bypass or finite failure differs");
	}
	return {};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM6RegionFilterContractTest, "VoxelLand.M6.Filters.Contract",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM6RegionFilterContractTest::RunTest(const FString& Parameters)
{
	const FString Error = Async(EAsyncExecution::ThreadPool, &FVoxelLandM6RegionContractFixture::Run).Get();
	return TestTrue(Error.IsEmpty() ? TEXT("M6 region filter arithmetic and admission") : Error, Error.IsEmpty());
}
#endif
