#if WITH_DEV_AUTOMATION_TESTS

#include "../Terrain/Runtime/VoxelLandRuntimeTestInputs.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Data/World/Streaming/VoxelLandWorldRecord.h"
#include "Generation/Collision/VoxelLandCollisionField.h"
#include "World/Collision/VoxelLandCollisionWorldView.h"
#include "World/VoxelLandWorld.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

class FVoxelLandWorldCollisionStreamingCommand final : public IAutomationLatentCommand
{
public:
	FVoxelLandWorldCollisionStreamingCommand(
		FAutomationTestBase& InTest, UWorld& InWorld, AVoxelLandWorld& InActor, UVoxelLandWorldConfig& InConfig)
		: Test(InTest)
		, World(InWorld)
		, Actor(InActor)
		, Config(&InConfig)
	{
		NegativeKey.Coordinate = FIntVector(-1, 0, 0);
	}

	bool Update() override
	{
		if (FPlatformTime::Seconds() - StartedAt > TimeoutSeconds)
		{
			Test.AddError(FString::Printf(TEXT("Collision streaming timed out: %s"), *Actor.LastInitializationError));
			return Finish();
		}
		Actor.Tick(TickSeconds);
		if (Actor.bInitializing)
		{
			return false;
		}
		if (!Test.TestTrue(TEXT("Terrain initialized"), Actor.HasWorldStateForTests()))
		{
			Test.AddError(Actor.LastInitializationError);
			return Finish();
		}
		if (bReplacing)
		{
			if (!Actor.FindChunk(FVoxelLandChunkKey{}))
			{
				return false;
			}
			VerifyReplacement();
			return Finish();
		}
		if (!bRequested)
		{
			if (!RequestFixtures())
			{
				return Finish();
			}
			bRequested = true;
			return false;
		}
		if (!Actor.FindChunk(NegativeKey) || !Actor.FindChunk(FVoxelLandChunkKey{}))
		{
			return false;
		}
		bReplacing = VerifyCompletedFixtures();
		return bReplacing ? false : Finish();
	}

private:
	bool RequestFixtures()
	{
		const FVoxelLandChunkKey Negative[] = {NegativeKey};
		if (!Test.TestTrue(TEXT("Swept bounds admit both chunks across a negative seam"),
				Actor.UpdateCollisionDemandForBounds(First, FBox(FVector(-140, 300, 200), FVector(140, 500, 400))))
			|| !Test.TestTrue(
				TEXT("Independent consumer overlaps the same chunk"), Actor.UpdateCollisionDemand(Second, Negative)))
		{
			return false;
		}
		FVoxelLandCollisionWorldView View;
		if (!Test.TestTrue(
				TEXT("A lease allows capture before asynchronous generation"), Actor.CaptureCollisionView(First, View)))
		{
			return false;
		}
		double Density = 0.0;
		Test.TestTrue(TEXT("A pending in-bounds key reports missing"),
			View.Field->Sample(FVector3d(-100, 400, 400), Density) == EVoxelLandCollisionFieldStatus::Missing);
		Test.TestTrue(TEXT("Finite exterior is available even with an empty lease"),
			Actor.UpdateCollisionDemandForBounds(Exterior, FBox(FVector(8000, 300, 200), FVector(8200, 500, 400)))
				&& Actor.CaptureCollisionView(Exterior, View)
				&& View.Field->Sample(FVector3d(8100, 400, 400), Density) == EVoxelLandCollisionFieldStatus::Available
				&& Density > 0.0);
		Actor.ReleaseCollisionDemand(Exterior);
		return true;
	}

	bool VerifyCompletedFixtures()
	{
		FVoxelLandChunkKey InvalidKey = NegativeKey;
		InvalidKey.LOD = 1;
		const FVoxelLandChunkKey Invalid[] = {InvalidKey};
		Test.TestFalse(TEXT("Reject non-LOD-zero replacement"), Actor.UpdateCollisionDemand(First, Invalid));
		FVoxelLandChunkKey UnadmittedKey;
		UnadmittedKey.Coordinate = FIntVector(1, 0, 0);
		const FVoxelLandChunkKey Unadmitted[] = {UnadmittedKey};
		Test.TestFalse(TEXT("The resident cap protects other consumers and explicit startup demand"),
			Actor.UpdateCollisionDemand(First, Unadmitted));
		if (!Test.TestTrue(
				TEXT("Rejected replacement keeps the original lease"), Actor.CaptureCollisionView(First, CapturedView)))
		{
			return false;
		}
		double PositiveDensity = 0.0;
		Test.TestTrue(TEXT("Both sides of the two-chunk seam are resident"),
			CapturedView.Field->Sample(FVector3d(-100, 400, 400), NegativeDensity)
					== EVoxelLandCollisionFieldStatus::Available
				&& CapturedView.Field->Sample(FVector3d(100, 400, 400), PositiveDensity)
					   == EVoxelLandCollisionFieldStatus::Available);
		Test.TestTrue(
			TEXT("Snapshot is current before generation changes"), Actor.IsCollisionViewCurrent(CapturedView));

		Actor.ReleaseCollisionDemand(First);
		Actor.Tick(TickSeconds);
		Test.TestTrue(
			TEXT("Second consumer keeps its overlapping chunk resident"), Actor.FindChunk(NegativeKey).IsValid());
		Test.TestTrue(
			TEXT("Legacy explicit requests can share a collision-resident key"), Actor.RequestChunk(NegativeKey));
		Actor.ReleaseCollisionDemand(Second);
		Actor.Tick(TickSeconds);
		Test.TestTrue(TEXT("Lease release preserves explicit ownership"), Actor.FindChunk(NegativeKey).IsValid());
		Actor.ReleaseChunk(NegativeKey);
		Actor.Tick(TickSeconds);
		Test.TestFalse(TEXT("Unowned headless chunk retires"), Actor.FindChunk(NegativeKey).IsValid());

		const FVoxelLandChunkKey Startup[] = {FVoxelLandChunkKey{}};
		return Test.TestTrue(TEXT("Leave a live lease for replacement"), Actor.UpdateCollisionDemand(First, Startup))
			&& Test.TestTrue(TEXT("Start terrain generation replacement"),
				Actor.InitializeFromConfig(Config.Get(), FIntVector::ZeroValue));
	}

	void VerifyReplacement()
	{
		Test.TestFalse(TEXT("A retired generation cannot accept a result"), Actor.IsCollisionViewCurrent(CapturedView));
		FVoxelLandCollisionWorldView Out;
		Test.TestFalse(TEXT("Replacement clears prior consumer leases"), Actor.CaptureCollisionView(First, Out));
		double SnapshotDensity = 0.0;
		Test.TestTrue(TEXT("Eviction and replacement do not mutate captured sample data"),
			CapturedView.Field->Sample(FVector3d(-100, 400, 400), SnapshotDensity)
				== EVoxelLandCollisionFieldStatus::Available);
		Test.TestEqual(TEXT("Retained chunk contains the original density"), SnapshotDensity, NegativeDensity);
		const TActorIterator<AVoxelLandRegion> Regions(&World);
		Test.TestFalse(TEXT("Headless collision did not spawn terrain regions"), bool(Regions));
	}

	bool Finish()
	{
		Actor.Shutdown();
		World.DestroyWorld(false);
		GEngine->DestroyWorldContext(&World);
		World.RemoveFromRoot();
		return true;
	}

	static constexpr float TickSeconds = 0.016f;
	static constexpr double TimeoutSeconds = 20.0;
	FAutomationTestBase& Test;
	UWorld& World;
	AVoxelLandWorld& Actor;
	TStrongObjectPtr<UVoxelLandWorldConfig> Config;
	FVoxelLandChunkKey NegativeKey;
	FVoxelLandCollisionWorldView CapturedView;
	double NegativeDensity = 0.0;
	const FGuid First = FGuid::NewGuid();
	const FGuid Second = FGuid::NewGuid();
	const FGuid Exterior = FGuid::NewGuid();
	const double StartedAt = FPlatformTime::Seconds();
	bool bRequested = false;
	bool bReplacing = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandWorldCollisionRetryTest, "VoxelLand.Collision.World.Retry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandWorldCollisionRetryTest::RunTest(const FString& Parameters)
{
	VoxelLandWorldPrivate::FRecord Record;
	Record.CollisionOwners = 1;
	Record.bFailed = true;
	Record.DeferCollisionRetry(10.0);
	Record.QueueDueCollisionRetry(10.24);
	TestTrue(TEXT("A collision failure retains its backoff"), Record.bFailed && !Record.bQueued);
	Record.QueueDueCollisionRetry(10.25);
	TestTrue(TEXT("First due attempt requeues the data job"), !Record.bFailed && Record.bQueued);

	Record.bQueued = false;
	Record.bFailed = true;
	Record.DeferCollisionRetry(11.0);
	Record.QueueDueCollisionRetry(11.49);
	TestTrue(TEXT("Retry delay grows"), Record.bFailed && !Record.bQueued);
	Record.QueueDueCollisionRetry(11.5);
	TestTrue(TEXT("Second due attempt requeues"), !Record.bFailed && Record.bQueued);

	Record.bQueued = false;
	Record.bFailed = true;
	Record.DeferCollisionRetry(12.0);
	Record.QueueDueCollisionRetry(100.0);
	TestTrue(TEXT("Persistent failures stop retrying"), Record.bFailed && !Record.bQueued);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandWorldCollisionStreamingTest, "VoxelLand.Collision.World.Streaming",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandWorldCollisionStreamingTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Engine available"), GEngine))
	{
		return false;
	}
	const TStrongObjectPtr<UVoxelLandWorldConfig> Config(NewObject<UVoxelLandWorldConfig>());
	UVoxelLandTerrainGraph* const Graph = NewObject<UVoxelLandTerrainGraph>(Config.Get());
	FString Error;
	if (!FVoxelLandM4RuntimeTestInputs::MakeProgram(*Graph, Error))
	{
		AddError(Error);
		return false;
	}
	Config->TerrainSource = EVoxelLandTerrainSource::DirectDensity;
	Config->WorldExtent = EVoxelLandWorldExtent::Finite;
	Config->Graph = Graph;
	Config->DirectVoxelSizeCm = 100.0f;
	Config->DensityBounds.MinChunk = FIntVector(-1, 0, 0);
	Config->DensityBounds.MaxChunkExclusive = FIntVector(2, 1, 1);

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World created"), World))
	{
		return false;
	}
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	AVoxelLandWorld* const Actor = World->SpawnActor<AVoxelLandWorld>();
	if (TestNotNull(TEXT("Terrain actor created"), Actor))
	{
		Actor->bDataOnly = true;
		Actor->MaxResidentChunks = 2;
		Actor->DispatchBeginPlay();
		if (TestTrue(TEXT("Start headless collision terrain"),
				Actor->InitializeFromConfig(Config.Get(), FIntVector::ZeroValue)))
		{
			ADD_LATENT_AUTOMATION_COMMAND(
				FVoxelLandWorldCollisionStreamingCommand(*this, *World, *Actor, *Config.Get()));
			return true;
		}
		AddError(Actor->LastInitializationError);
		Actor->Shutdown();
	}
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return false;
}

#endif
