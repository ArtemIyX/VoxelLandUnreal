#if WITH_DEV_AUTOMATION_TESTS
#include "../Planning/VoxelLandCheckpointTestFixture.h"

#include "Async/Async.h"
#include "Misc/AutomationTest.h"
#include <cmath>
#include <functional>

class FVoxelLandM7FiniteLodFixture final
{
public:
	static FString Run();

private:
	using FFixture = FVoxelLandM7CheckpointTestFixture;
	using FGridPtr = FFixture::FGridPtr;
	using FGeneratorPtr = FFixture::FGeneratorPtr;
	using FChunkPtr = FFixture::FChunkPtr;

	static bool CheckAnalytic(const FVoxelLandGenerator& Generator, const FVoxelLandChunkKey& Key);
	static bool CheckCoincident(const FVoxelLandGenerator& Generator, const FVoxelLandChunkKey& CoarseKey);
	static bool CheckDemandAndCancellation(FVoxelLandGeneratorSettings Settings, const FVoxelLandGenerator& Generator);
	static bool CountPoll(int32& Polls)
	{
		++Polls;
		return false;
	}
	static bool CancelOnZero(int32& Remaining) { return --Remaining == 0; }
};

bool FVoxelLandM7FiniteLodFixture::CheckAnalytic(const FVoxelLandGenerator& Generator, const FVoxelLandChunkKey& Key)
{
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	FChunkPtr Chunk;
	if (Generator.CreateWorker(Worker) || Generator.GenerateChunk(Key, *Worker, Chunk) || !Chunk || Chunk->Key != Key
		|| Chunk->Density.Num() != FVoxelLandChunkData::SampleCount)
	{
		return false;
	}
	const int32 Stride = Key.LOD == 0 ? 1 : FFixture::CoarseStride;
	const FInt64Vector First = FInt64Vector(Key.Coordinate) * int64(FFixture::Cells);
	for (int32 Z = -1; Z <= FFixture::Cells + 1; ++Z)
	{
		for (int32 Y = -1; Y <= FFixture::Cells + 1; ++Y)
		{
			for (int32 X = -1; X <= FFixture::Cells + 1; ++X)
			{
				const FIntVector Sample(X, Y, Z);
				const FInt64Vector PointCm = (First + FInt64Vector(Sample)) * int64(Stride * FFixture::VoxelSizeCm);
				const double Height =
					FFixture::HeightCm(FVector(double(PointCm.X), double(PointCm.Y), double(PointCm.Z)));
				const double Q = std::round((double(PointCm.Z) - Height) * 1024.0 / FFixture::VoxelSizeCm);
				const int16 Expected = int16(FMath::Clamp(Q, double(MIN_int16), double(MAX_int16)));
				if (Expected != Chunk->Density[FFixture::Index(Sample)])
				{
					return false;
				}
			}
		}
	}
	return true;
}

bool FVoxelLandM7FiniteLodFixture::CheckCoincident(
	const FVoxelLandGenerator& Generator, const FVoxelLandChunkKey& CoarseKey)
{
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	FChunkPtr Coarse;
	if (Generator.CreateWorker(Worker) || Generator.GenerateChunk(CoarseKey, *Worker, Coarse) || !Coarse)
	{
		return false;
	}
	for (int32 Child = 7; Child >= 0; --Child)
	{
		const FIntVector Offset(Child & 1, (Child >> 1) & 1, (Child >> 2) & 1);
		const FVoxelLandChunkKey ChildKey{CoarseKey.Coordinate * 2 + Offset};
		FChunkPtr Fine;
		if (Generator.GenerateChunk(ChildKey, *Worker, Fine) || !Fine)
		{
			return false;
		}
		for (int32 Z = 0; Z <= FFixture::Cells; Z += 2)
		{
			for (int32 Y = 0; Y <= FFixture::Cells; Y += 2)
			{
				for (int32 X = 0; X <= FFixture::Cells; X += 2)
				{
					const FIntVector FineSample(X, Y, Z);
					const FIntVector CoarseSample = Offset * (FFixture::Cells / 2) + FineSample / 2;
					if (Fine->Density[FFixture::Index(FineSample)] != Coarse->Density[FFixture::Index(CoarseSample)])
					{
						return false;
					}
				}
			}
		}
	}
	FChunkPtr Again;
	return !Generator.GenerateChunk(CoarseKey, *Worker, Again) && Again && Again->Hash == Coarse->Hash
		&& Again->Density == Coarse->Density;
}

bool FVoxelLandM7FiniteLodFixture::CheckDemandAndCancellation(
	FVoxelLandGeneratorSettings Settings, const FVoxelLandGenerator& Generator)
{
	FGridPtr StoredOnly, CompatibilityOnly, WithSlope;
	if (!FFixture::CreateTiltedGrid(-200, 68, 100, StoredOnly)
		|| !FFixture::CreateTiltedGrid(-300, 70, 100, CompatibilityOnly)
		|| !FFixture::CreateTiltedGrid(-400, 72, 100, WithSlope))
	{
		return false;
	}
	FGeneratorPtr Raw, TooNarrowForSmooth, Smoothed, TooNarrowForSlope, Full;
	if (!FFixture::CreateGenerator(Settings, StoredOnly, false, false, Raw)
		|| !FFixture::CreateGenerator(Settings, StoredOnly, true, false, TooNarrowForSmooth)
		|| !FFixture::CreateGenerator(Settings, CompatibilityOnly, true, false, Smoothed)
		|| !FFixture::CreateGenerator(Settings, CompatibilityOnly, true, true, TooNarrowForSlope)
		|| !FFixture::CreateGenerator(Settings, WithSlope, true, true, Full))
	{
		return false;
	}
	const FVoxelLandChunkKey Coarse{FIntVector::ZeroValue, FFixture::CoarseLod};
	if (Raw->ValidateM4ChunkDemand(Coarse)
		|| TooNarrowForSmooth->ValidateM4ChunkDemand(Coarse).Code != EVoxelLandErrorCode::InvalidParameter
		|| Smoothed->ValidateM4ChunkDemand(Coarse)
		|| TooNarrowForSlope->ValidateM4ChunkDemand(Coarse).Code != EVoxelLandErrorCode::InvalidParameter
		|| Full->ValidateM4ChunkDemand(Coarse))
	{
		return false;
	}

	FVoxelLandM6Stamp Stamp;
	Stamp.Id = FGuid(0x4d37, 1, 0, 1);
	Stamp.RadiusCm = 100;
	Settings.M6Stamps.Stamps = MakeArrayView(&Stamp, 1);
	FGeneratorPtr Active, Disabled;
	if (!FFixture::CreateGenerator(Settings, WithSlope, false, false, Active))
	{
		return false;
	}
	Settings.M6Stamps.bEnabled = false;
	if (!FFixture::CreateGenerator(Settings, WithSlope, false, false, Disabled) || Active->ValidateM4ChunkDemand(Coarse)
		|| Disabled->ValidateM4ChunkDemand(Coarse))
	{
		return false;
	}

	const FVoxelLandChunkKey TooFarXY{FIntVector(32767, 0, 0), FFixture::CoarseLod};
	const FVoxelLandChunkKey TooFarZ{FIntVector(0, 0, 313), FFixture::CoarseLod};
	const FVoxelLandChunkKey TooCoarse{FIntVector::ZeroValue, 6};
	if (Generator.ValidateM4ChunkDemand(TooFarXY).Code != EVoxelLandErrorCode::NonFiniteCoordinate
		|| Generator.ValidateM4ChunkDemand(TooFarZ).Code != EVoxelLandErrorCode::NonFiniteCoordinate
		|| Generator.ValidateM4ChunkDemand(TooCoarse).Code != EVoxelLandErrorCode::UnsupportedLod)
	{
		return false;
	}

	TUniquePtr<FVoxelLandWorkerContext> Worker;
	FChunkPtr Valid;
	int32 Polls = 0;
	if (Generator.CreateWorker(Worker)
		|| Generator.GenerateChunkCancellable(Coarse, *Worker, Valid, std::bind_front(&CountPoll, std::ref(Polls)))
		|| !Valid)
	{
		return false;
	}
	FChunkPtr Protected = Valid;
	if (Generator.GenerateChunk(TooFarXY, *Worker, Protected).Code != EVoxelLandErrorCode::NonFiniteCoordinate
		|| Protected != Valid)
	{
		return false;
	}
	int32 Remaining = Polls - 2; // The final two polls surround the hash after sample selection.
	const FVoxelLandError Cancel = Generator.GenerateChunkCancellable(
		Coarse, *Worker, Protected, std::bind_front(&CancelOnZero, std::ref(Remaining)));
	return Cancel.Code == EVoxelLandErrorCode::Cancelled && Cancel.Message.Contains(TEXT("sample selection"))
		&& Protected == Valid;
}

FString FVoxelLandM7FiniteLodFixture::Run()
{
	FVoxelLandGeneratorSettings Settings;
	FGridPtr Grid;
	FGeneratorPtr Raw, Shaped;
	if (!FFixture::CreateSettings(Settings) || !FFixture::CreateTiltedGrid(-16000, 32, 1000, Grid)
		|| !FFixture::CreateGenerator(Settings, Grid, false, false, Raw)
		|| !FFixture::CreateGenerator(Settings, Grid, true, true, Shaped))
	{
		return TEXT("Finite LOD checkpoint setup failed");
	}
	const FVoxelLandChunkKey Fine{FIntVector(-1, -1, 0)};
	const FVoxelLandChunkKey Coarse{FIntVector(-1, -1, 0), FFixture::CoarseLod};
	if (!CheckAnalytic(*Raw, Fine) || !CheckAnalytic(*Raw, Coarse))
	{
		return TEXT("Ordinary finite LOD0 or LOD1 density differs from the tilted analytic reference");
	}
	if (!CheckCoincident(*Shaped, Coarse))
	{
		return TEXT("LOD1 did not reproduce coincident LOD0 values with base-lattice smoothing and slope reads");
	}
	if (!CheckDemandAndCancellation(Settings, *Shaped))
	{
		return TEXT("LOD1 footprint, M6 admission, precision or atomic cancellation differs");
	}
	return {};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM7FiniteLodTest, "VoxelLand.M7.Lod.FiniteCoincidence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM7FiniteLodTest::RunTest(const FString& Parameters)
{
	const FString Error = Async(EAsyncExecution::ThreadPool, &FVoxelLandM7FiniteLodFixture::Run).Get();
	return TestTrue(Error.IsEmpty() ? TEXT("Aligned finite field through ordinary LOD0 and LOD1 generation") : Error,
		Error.IsEmpty());
}
#endif
