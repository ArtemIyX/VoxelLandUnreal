#if WITH_DEV_AUTOMATION_TESTS
#include "VoxelLandRuntimeTestInputs.h"

#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Assets/Materials/VoxelLandMaterialPalette.h"
#include "Data/Assets/Materials/VoxelLandMaterialSetup.h"
#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "World/VoxelLandWorld.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

class FVoxelLandHeadlessMaterialsCommand final : public IAutomationLatentCommand
{
public:
	FVoxelLandHeadlessMaterialsCommand(
		FAutomationTestBase& InTest, UWorld& InWorld, AVoxelLandWorld& InActor, UVoxelLandWorldConfig& InConfig)
		: Test(InTest)
		, World(InWorld)
		, Actor(InActor)
		, Config(&InConfig)
	{
	}

	bool Update() override
	{
		if (FPlatformTime::Seconds() - StartedAt > TimeoutSeconds)
		{
			Test.AddError(
				FString::Printf(TEXT("Headless material fixture timed out: %s"), *Actor.LastInitializationError));
			return Finish();
		}

		Actor.Tick(0.016f);
		if (Actor.bInitializing)
		{
			return false;
		}
		const TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk = Actor.FindChunk(FVoxelLandChunkKey{});
		if (!Chunk)
		{
			if (!Actor.LastInitializationError.IsEmpty())
			{
				Test.AddError(Actor.LastInitializationError);
				return Finish();
			}
			return false;
		}
		if (!Setup.IsValid())
		{
			Baseline = Chunk;
			Setup.Reset(NewObject<UVoxelLandMaterialSetup>(GetTransientPackage()));
			Setup->Palette = TSoftObjectPtr<UVoxelLandMaterialPalette>(
				FSoftObjectPath(TEXT("/Game/VoxelLandHeadlessTestMissing.Palette")));
			Config->MaterialSetup = Setup.Get();
			if (!Test.TestTrue(TEXT("Data-only worlds accept a setup with missing render assets"),
					Actor.InitializeFromConfig(Config.Get(), FIntVector::ZeroValue)))
			{
				return Finish();
			}
			return false;
		}

		Test.TestTrue(TEXT("Data-only regeneration publishes a replacement"), Chunk != Baseline);
		Test.TestEqual(TEXT("Presentation settings do not change density identity"), Chunk->Hash, Baseline->Hash);
		Test.TestTrue(TEXT("No material instance was created"), Actor.TerrainMaterial == nullptr);
		const TActorIterator<AVoxelLandRegion> Regions(&World);
		Test.TestFalse(TEXT("No terrain regions were created"), bool(Regions));
		return Finish();
	}

private:
	bool Finish()
	{
		Actor.Shutdown();
		World.DestroyWorld(false);
		GEngine->DestroyWorldContext(&World);
		World.RemoveFromRoot();
		return true;
	}

	static constexpr double TimeoutSeconds = 20.0;
	FAutomationTestBase& Test;
	UWorld& World;
	AVoxelLandWorld& Actor;
	TStrongObjectPtr<UVoxelLandWorldConfig> Config;
	TStrongObjectPtr<UVoxelLandMaterialSetup> Setup;
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Baseline;
	double StartedAt = FPlatformTime::Seconds();
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandHeadlessMaterialsTest, "VoxelLand.Materials.Headless",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandHeadlessMaterialsTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Engine available"), GEngine))
	{
		return false;
	}
	TStrongObjectPtr<UVoxelLandWorldConfig> Config(NewObject<UVoxelLandWorldConfig>());
	UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(Config.Get());
	FString Error;
	if (!FVoxelLandM4RuntimeTestInputs::MakeProgram(*Graph, Error))
	{
		AddError(Error);
		return false;
	}
	Config->TerrainSource = EVoxelLandTerrainSource::DirectDensity;
	Config->WorldExtent = EVoxelLandWorldExtent::Infinite;
	Config->Graph = Graph;
	Config->HorizontalRadius = 1;
	Config->VerticalRadius = 1;

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World created"), World))
	{
		return false;
	}
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	AVoxelLandWorld* Actor = World->SpawnActor<AVoxelLandWorld>();
	if (TestNotNull(TEXT("Terrain actor created"), Actor))
	{
		Actor->bDataOnly = true;
		Actor->DispatchBeginPlay();
		if (TestTrue(TEXT("Start data-only world"), Actor->InitializeFromConfig(Config.Get(), FIntVector::ZeroValue)))
		{
			ADD_LATENT_AUTOMATION_COMMAND(FVoxelLandHeadlessMaterialsCommand(*this, *World, *Actor, *Config.Get()));
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
