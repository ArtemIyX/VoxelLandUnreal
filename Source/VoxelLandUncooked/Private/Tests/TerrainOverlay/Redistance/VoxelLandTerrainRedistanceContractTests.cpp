#if WITH_DEV_AUTOMATION_TESTS
#include "../Filters/VoxelLandTerrainRegionFilterTestFixture.h"

#include "Async/Async.h"
#include "Misc/AutomationTest.h"
#include <functional>

class FVoxelLandM6RedistanceContractFixture final
{
public:
	static FString Run();

private:
	using FBase = FVoxelLandM6RegionFilterTestFixture;
	using FFixture = FBase::FFixture;
	using FChunkPtr = FBase::FChunkPtr;
	using FGeneratorPtr = FBase::FGeneratorPtr;
	using FGridPtr = FBase::FGridPtr;
	static bool CheckBands(FVoxelLandGeneratorSettings Settings);
	static bool CheckMinimumSign(FVoxelLandGeneratorSettings Settings);
	static bool CheckAdmissionAndUnchanged(FVoxelLandGeneratorSettings Settings, const FGridPtr& Wide);
	static bool CheckCancellation(const FVoxelLandGenerator& Generator);
	static int16 Sample(const FChunkPtr& Chunk, const FIntVector& Local)
	{
		return Chunk->Density[FFixture::SampleIndex(Local, FFixture::Samples)];
	}
	static bool CountPoll(int32& Polls)
	{
		++Polls;
		return false;
	}
	static bool CancelOnZero(int32& Remaining) { return --Remaining == 0; }
};

bool FVoxelLandM6RedistanceContractFixture::CheckBands(FVoxelLandGeneratorSettings Settings)
{
	constexpr int32 GridCells = 50;
	constexpr int32 OriginIndex = 10;
	TArray<int32> Heights;
	Heights.Init(200 * 256, (GridCells + 1) * (GridCells + 1));
	Heights[OriginIndex + (GridCells + 1) * OriginIndex] = 300 * 256;
	FGridPtr Grid;
	if (!FVoxelLandM4HeightGrid::Create(-1000, -1000, GridCells, GridCells, 100, Heights, Grid))
	{
		return false;
	}
	const FVoxelLandM6FilterStage Smooth =
		FBase::Stage(1, EVoxelLandM6FilterKind::Smooth, FInt64Vector(0, 0, 500), FInt64Vector(0, 0, 500));
	const TConstArrayView<FVoxelLandM6FilterStage> Filters = MakeArrayView(&Smooth, 1);
	FGeneratorPtr Disabled, BandOne, BandTwo;
	if (!FBase::CreateFinite(Settings, Grid, Filters, false, Disabled))
	{
		return false;
	}
	Settings.M6Redistance.bEnabled = true;
	if (!FBase::CreateFinite(Settings, Grid, Filters, false, BandOne))
	{
		return false;
	}
	Settings.M6Redistance.BandVoxels = 2;
	if (!FBase::CreateFinite(Settings, Grid, Filters, false, BandTwo))
	{
		return false;
	}

	const TConstArrayView<uint8> One = BandOne->GetM6RedistanceIdentity();
	const TConstArrayView<uint8> Two = BandTwo->GetM6RedistanceIdentity();
	const FVoxelLandChunkKey Key;
	FChunkPtr Source, Near, Full;
	if (!Disabled->GetM6RedistanceIdentity().IsEmpty() || One.Num() != 9 || Two.Num() != 9 || One[0] != 'M'
		|| One[4] != 1 || One[8] != 1 || Two[8] != 2 || !FBase::Generate(*Disabled, Key, Source)
		|| !FBase::Generate(*BandOne, Key, Near) || !FBase::Generate(*BandTwo, Key, Full))
	{
		return false;
	}

	// Only the target sample at z=500 was smoothed. Its original +2048 is
	// replaced by the rounded neighborhood mean 26624/9 = 2958. The nearest
	// crossing is at z=300, the upper endpoint of the edge starting at z=200.
	// Other columns rise at z=200 and cannot offer a closer candidate.
	const FIntVector Target(0, 0, 5);
	return Sample(Source, Target) == 2958 && Sample(Near, Target) == 2958 && Sample(Full, Target) == 2048
		&& Sample(Full, FIntVector(0, 0, 3)) == 0 && Sample(Full, FIntVector(0, 0, 2)) < 0
		&& CheckCancellation(*BandTwo);
}

bool FVoxelLandM6RedistanceContractFixture::CheckMinimumSign(FVoxelLandGeneratorSettings Settings)
{
	constexpr int32 GridCells = 50;
	constexpr int32 OriginIndex = 10;
	TArray<int32> Heights;
	Heights.Init(-8000 * 256, (GridCells + 1) * (GridCells + 1));
	// At the origin this Q8 height gives density -1. Lateral air saturates,
	// so the interpolated Q10 crossing rounds directly onto this sample.
	Heights[OriginIndex + (GridCells + 1) * OriginIndex] = 25;
	FGridPtr Grid;
	if (!FVoxelLandM4HeightGrid::Create(-1000, -1000, GridCells, GridCells, 100, Heights, Grid))
	{
		return false;
	}
	Settings.M6Redistance.bEnabled = true;
	FGeneratorPtr Generator;
	FChunkPtr Chunk;
	return FBase::CreateFinite(Settings, Grid, {}, false, Generator) && FBase::Generate(*Generator, {}, Chunk)
		&& Sample(Chunk, FIntVector(0)) == -1;
}

bool FVoxelLandM6RedistanceContractFixture::CheckAdmissionAndUnchanged(
	FVoxelLandGeneratorSettings Settings, const FGridPtr& Wide)
{
	FGeneratorPtr Baseline, WithRedistance;
	if (!FBase::CreateFinite(Settings, Wide, {}, false, Baseline))
	{
		return false;
	}
	Settings.M6Redistance.bEnabled = true;
	Settings.M6Redistance.BandVoxels = 2;
	if (!FBase::CreateFinite(Settings, Wide, {}, false, WithRedistance)
		|| !WithRedistance->GetM6StampIdentity().IsEmpty() || !WithRedistance->GetM6FilterIdentity().IsEmpty()
		|| WithRedistance->GetM6RedistanceIdentity().IsEmpty())
	{
		return false;
	}
	const FVoxelLandChunkKey Key;
	FChunkPtr Original, NoCandidates;
	if (!FBase::Generate(*Baseline, Key, Original) || !FBase::Generate(*WithRedistance, Key, NoCandidates)
		|| Original->Hash != NoCandidates->Hash || Original->Density != NoCandidates->Density)
	{
		return false;
	}

	TArray<int32> Heights;
	Heights.Init(0, 39 * 39);
	FGridPtr Narrow;
	FGeneratorPtr Boundary;
	// This admits the stored chunk and a two-voxel halo, but not the
	// additional endpoint read needed by a two-voxel re-distance band.
	if (!FVoxelLandM4HeightGrid::Create(-300, -300, 38, 38, 100, Heights, Narrow)
		|| !FBase::CreateFinite(Settings, Narrow, {}, false, Boundary) || !Boundary->ValidateM4ChunkDemand(Key))
	{
		return false;
	}
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	FChunkPtr Protected = Original;
	if (Boundary->CreateWorker(Worker) || !Boundary->GenerateChunk(Key, *Worker, Protected) || Protected != Original)
	{
		return false;
	}

	TArray<FVoxelLandM6FilterStage> SixFilters;
	for (uint32 Id = 1; Id <= 6; ++Id)
	{
		SixFilters.Add(FBase::Stage(Id, EVoxelLandM6FilterKind::Smooth, FInt64Vector(5000), FInt64Vector(5100)));
	}
	FGeneratorPtr Rejected = Baseline;
	if (FBase::CreateFinite(Settings, Wide, SixFilters, false, Rejected) || Rejected != Baseline)
	{
		return false;
	}
	Settings.M6Redistance.BandVoxels = 3;
	if (FBase::CreateFinite(Settings, Wide, {}, false, Rejected) || Rejected != Baseline)
	{
		return false;
	}
	Settings.M6Redistance.BandVoxels = 1;
	FGeneratorPtr ExactCap;
	return FBase::CreateFinite(Settings, Wide, SixFilters, false, ExactCap);
}

bool FVoxelLandM6RedistanceContractFixture::CheckCancellation(const FVoxelLandGenerator& Generator)
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
	int32 Remaining = Polls - 2; // The final two polls after re-distance precede hash and publication.
	FChunkPtr Protected = First;
	const FVoxelLandError Error = Generator.GenerateChunkCancellable(
		Key, *Worker, Protected, std::bind_front(&CancelOnZero, std::ref(Remaining)));
	return Error.Code == EVoxelLandErrorCode::Cancelled && Error.Message.Contains(TEXT("re-distance"))
		&& Protected == First;
}

FString FVoxelLandM6RedistanceContractFixture::Run()
{
	FVoxelLandGeneratorSettings Settings;
	FGridPtr Grid;
	if (!FFixture::CreateInputs(-8000, Settings, Grid))
	{
		return TEXT("Finite re-distance reference setup failed");
	}
	if (!CheckBands(Settings) || !CheckMinimumSign(Settings))
	{
		return TEXT("Re-distance band inclusion, sign/zero preservation, identity or cancellation differs");
	}
	if (!CheckAdmissionAndUnchanged(Settings, Grid))
	{
		return TEXT("Re-distance no-candidate, finite-boundary or combined halo admission differs");
	}
	return {};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM6RedistanceContractTest, "VoxelLand.M6.Redistance.Contract",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM6RedistanceContractTest::RunTest(const FString& Parameters)
{
	const FString Error = Async(EAsyncExecution::ThreadPool, &FVoxelLandM6RedistanceContractFixture::Run).Get();
	return TestTrue(Error.IsEmpty() ? TEXT("Bounded fixed-point re-distance contract") : Error, Error.IsEmpty());
}
#endif
