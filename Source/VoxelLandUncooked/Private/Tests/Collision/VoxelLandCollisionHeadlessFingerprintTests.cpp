#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "World/VoxelLandWorld.h"

class FVoxelLandCollisionFingerprintCommand final : public IAutomationLatentCommand
{
public:
	FVoxelLandCollisionFingerprintCommand(
		FAutomationTestBase& InTest, UWorld& InWorld, AVoxelLandWorld& InTerrain, UVoxelLandWorldConfig& InConfig)
		: Test(InTest)
		, World(InWorld)
		, Terrain(InTerrain)
		, Config(&InConfig)
	{
	}

	bool Update() override
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - Start > 15.0 || !Terrain.LastInitializationError.IsEmpty())
		{
			Test.AddError(FString::Printf(
				TEXT("Headless startup failed for %s. %s"), *Config->GetName(), *Terrain.LastInitializationError));
			return Finish();
		}
		Terrain.Tick(1.0f / 60.0f);
		const TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Data = Terrain.FindChunk(FVoxelLandChunkKey{});
		if (Terrain.bInitializing || !Data)
		{
			return false;
		}
		if (!FirstData)
		{
			FirstData = Data;
			FirstMs = (Now - Start) * 1000.0;
			Start = Now;
			if (!Test.TestTrue(TEXT("Reinitialization admitted"),
					Terrain.InitializeFromConfig(Config.Get(), FIntVector::ZeroValue)))
			{
				return Finish();
			}
			return false;
		}
		Test.TestTrue(TEXT("Reinitialization publishes a different chunk payload"), Data != FirstData);
		Test.TestEqual(TEXT("A regenerated origin chunk has the same fingerprint"), Data->Hash, FirstData->Hash);
		const TActorIterator<AVoxelLandRegion> Region(&World);
		Test.TestFalse(TEXT("Headless startup did not create a render region"), bool(Region));
		Test.AddInfo(FString::Printf(TEXT("%s at (0,0,0) LOD 0. Hash %llu. First ready %.2f ms, regenerated %.2f ms."),
			*Config->GetName(), static_cast<unsigned long long>(Data->Hash), FirstMs, (Now - Start) * 1000.0));
		return Finish();
	}

private:
	bool Finish()
	{
		Terrain.Shutdown();
		World.DestroyWorld(false);
		GEngine->DestroyWorldContext(&World);
		World.RemoveFromRoot();
		return true;
	}

	FAutomationTestBase& Test;
	UWorld& World;
	AVoxelLandWorld& Terrain;
	TStrongObjectPtr<UVoxelLandWorldConfig> Config;
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> FirstData;
	double FirstMs = 0.0;
	double Start = FPlatformTime::Seconds();
};

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FVoxelLandCollisionFingerprintTest, "VoxelLand.Collision.World.HeadlessFingerprint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

void FVoxelLandCollisionFingerprintTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	Names = {TEXT("RollingHills"), TEXT("DensityMaterials")};
	Commands = {TEXT("/VoxelLand/Examples/WC_RollingHills.WC_RollingHills"),
		TEXT("/VoxelLand/Examples/TerrainMaterials/WC_DensityMaterials.WC_DensityMaterials")};
}

bool FVoxelLandCollisionFingerprintTest::RunTest(const FString& Parameters)
{
	UVoxelLandWorldConfig* Config = LoadObject<UVoxelLandWorldConfig>(nullptr, *Parameters);
	if (!TestNotNull(TEXT("Shipped World Config"), Config) || !TestNotNull(TEXT("Engine available"), GEngine))
	{
		return false;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World created"), World))
	{
		return false;
	}
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	AVoxelLandWorld* Terrain = World->SpawnActor<AVoxelLandWorld>();
	if (TestNotNull(TEXT("Terrain actor created"), Terrain))
	{
		Terrain->bDataOnly = true;
		Terrain->DispatchBeginPlay();
		if (TestTrue(
				TEXT("Headless initialization admitted"), Terrain->InitializeFromConfig(Config, FIntVector::ZeroValue)))
		{
			ADD_LATENT_AUTOMATION_COMMAND(FVoxelLandCollisionFingerprintCommand(*this, *World, *Terrain, *Config));
			return true;
		}
		AddError(Terrain->LastInitializationError);
		Terrain->Shutdown();
	}
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return false;
}

#endif
