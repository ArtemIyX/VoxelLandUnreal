#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "../TerrainOverlay/Stamps/VoxelLandTerrainSphereTestFixture.h"
#include "Async/Async.h"
#include "Generation/Collision/VoxelLandCollisionField.h"
#include "World/Collision/VoxelLandWalkMotor.h"
#include "World/Collision/VoxelLandWalkSettings.h"
#include "World/Collision/VoxelLandWalkState.h"

class FVoxelLandWalkReplayFixture final
{
public:
	static FString Run();

private:
	static bool Generate(
		const FVoxelLandGenerator& Generator, bool bIsReverse, FVoxelLandCollisionField::FChunkMap& OutChunks);
	static FString Replay(const FVoxelLandCollisionField& Authority, const FVoxelLandCollisionField& Prediction);
};

bool FVoxelLandWalkReplayFixture::Generate(
	const FVoxelLandGenerator& Generator, bool bIsReverse, FVoxelLandCollisionField::FChunkMap& OutChunks)
{
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	if (Generator.CreateWorker(Worker))
	{
		return false;
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FVoxelLandChunkKey Key;
		Key.Coordinate = FIntVector((bIsReverse ? 1 - Index : Index) - 1, 0, 0);
		FVoxelLandCollisionField::FChunkPtr Chunk;
		if (Generator.GenerateChunkCancellable(Key, *Worker, Chunk, &FVoxelLandM6SphereTestFixture::NeverCancel)
			|| !Chunk)
		{
			return false;
		}
		OutChunks.Add(Key, MoveTemp(Chunk));
	}
	return true;
}

FString FVoxelLandWalkReplayFixture::Replay(
	const FVoxelLandCollisionField& Authority, const FVoxelLandCollisionField& Prediction)
{
	constexpr int32 CorrectionDelayTicks = 9;
	constexpr int32 StepCount = 100;
	const FVector2d Input(1.0, 0.0);
	const FVoxelLandWalkSettings Settings;
	FVoxelLandWalkState Initial;
	Initial.Center = FVector3d(-300.0, 1600.0, 140.0);
	FVoxelLandWalkState Server = Initial;
	FVoxelLandWalkState Client = Initial;
	TArray<FVoxelLandWalkState> History;
	History.Reserve(StepCount);
	for (int32 Tick = 0; Tick < StepCount; ++Tick)
	{
		if (!FVoxelLandWalkMotor::Step(Authority, Settings, Input, Server)
			|| !FVoxelLandWalkMotor::Step(Prediction, Settings, Input, Client))
		{
			return FString::Printf(TEXT("Movement stalled over resident terrain at tick %d"), Tick);
		}
		History.Add(Server);
		if (Tick >= CorrectionDelayTicks)
		{
			FVoxelLandWalkState Replayed = History[Tick - CorrectionDelayTicks];
			for (int32 Replay = 0; Replay < CorrectionDelayTicks; ++Replay)
			{
				if (!FVoxelLandWalkMotor::Step(Prediction, Settings, Input, Replayed))
				{
					return TEXT("Delayed snapshot replay stalled");
				}
			}
			if (!Replayed.Center.Equals(Client.Center, 1.e-6) || Replayed.bIsGrounded != Client.bIsGrounded)
			{
				return FString::Printf(TEXT("Delayed snapshot replay diverged at tick %d"), Tick);
			}
		}
	}
	if (Client.Center.X <= 300.0 || !Client.bIsGrounded || Client.Center.Z <= 113.0 || Client.Center.Z >= 121.0)
	{
		return FString::Printf(TEXT("Walk did not cross the seam while grounded. Center %s grounded %d"),
			*Client.Center.ToString(), Client.bIsGrounded);
	}
	return {};
}

FString FVoxelLandWalkReplayFixture::Run()
{
	constexpr int32 PlaneHeightCm = 25;
	FVoxelLandGeneratorSettings GeneratorSettings;
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
	if (!FVoxelLandM6SphereTestFixture::CreateInputs(PlaneHeightCm, GeneratorSettings, Grid)
		|| FVoxelLandM6SphereTestFixture::CreateGenerator(GeneratorSettings, Grid, false, Generator) || !Generator)
	{
		return TEXT("Could not build the real finite Height fixture");
	}
	FVoxelLandCollisionField::FChunkMap ServerChunks;
	FVoxelLandCollisionField::FChunkMap ClientChunks;
	if (!Generate(*Generator, false, ServerChunks) || !Generate(*Generator, true, ClientChunks))
	{
		return TEXT("Could not generate both adjacent chunks on each simulated side");
	}

	FVoxelLandChunkKey MissingKey;
	MissingKey.Coordinate = FIntVector::ZeroValue;
	FVoxelLandCollisionField::FChunkMap PartialChunks = ClientChunks;
	PartialChunks.Remove(MissingKey);
	const double VoxelSizeCm = GeneratorSettings.Profile.VoxelSizeCm;
	const FVoxelLandCollisionField Partial(VoxelSizeCm, MoveTemp(PartialChunks));
	FVoxelLandWalkState Blocked;
	Blocked.Center = FVector3d(-10.0, 1600.0, 118.0);
	const FVoxelLandWalkState Before = Blocked;
	if (FVoxelLandWalkMotor::Step(Partial, FVoxelLandWalkSettings(), FVector2d(1.0, 0.0), Blocked)
		|| Blocked.Center != Before.Center || Blocked.VerticalSpeed != Before.VerticalSpeed)
	{
		return TEXT("A missing owner must keep the complete fixed-step state unchanged");
	}

	const FVoxelLandCollisionField Authority(VoxelSizeCm, MoveTemp(ServerChunks));
	const FVoxelLandCollisionField Prediction(VoxelSizeCm, MoveTemp(ClientChunks));
	return Replay(Authority, Prediction);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandWalkReplayTest, "VoxelLand.Collision.WalkReplay",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandWalkReplayTest::RunTest(const FString& Parameters)
{
	const FString Error = Async(EAsyncExecution::Thread, &FVoxelLandWalkReplayFixture::Run).Get();
	return TestTrue(
		Error.IsEmpty() ? TEXT("Two-chunk walking and nine-tick delayed state replay") : Error, Error.IsEmpty());
}

#endif
