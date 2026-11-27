#if WITH_DEV_AUTOMATION_TESTS && !UE_SERVER

#include "../Clipmap/Planning/VoxelLandCheckpointTestFixture.h"

#include "World/Collision/Chaos/VoxelLandChaosStats.h"
#include "World/VoxelLandWorld.h"
#include "Async/Async.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/PhysicsSettings.h"

static USphereComponent* SpawnChaosManagedSphere(UWorld& World, const FVector& Location)
{
	AActor* Prop = World.SpawnActor<AActor>();
	if (!Prop)
	{
		return nullptr;
	}
	USphereComponent* Sphere = NewObject<USphereComponent>(Prop);
	Prop->SetRootComponent(Sphere);
	Prop->AddInstanceComponent(Sphere);
	Sphere->InitSphereRadius(45.0f);
	Sphere->SetMobility(EComponentMobility::Movable);
	Sphere->SetGenerateOverlapEvents(false);
	Sphere->SetCollisionProfileName(FName(TEXT("PhysicsActor")));
	Sphere->SetWorldLocation(Location);
	Sphere->RegisterComponent();
	return Sphere;
}

class FVoxelLandChaosManagedPropCommand final : public IAutomationLatentCommand
{
public:
	using FHeightFixture = FVoxelLandM7CheckpointTestFixture;

	FVoxelLandChaosManagedPropCommand(FAutomationTestBase& InTest, UWorld& InWorld, AVoxelLandWorld& InTerrain,
		USphereComponent& InSphere, const FGuid& InConsumer)
		: Test(InTest)
		, World(InWorld)
		, Terrain(InTerrain)
		, Sphere(InSphere)
		, Consumer(InConsumer)
	{
	}

	static FHeightFixture::FGeneratorPtr MakeFlatGenerator()
	{
		constexpr int32 GridCells = 16;
		constexpr int32 GridSpacingCm = 1000;
		constexpr int32 GridOriginCm = -8000;
		TArray<int32> Heights;
		Heights.Init(1100 * 256, (GridCells + 1) * (GridCells + 1));
		FVoxelLandGeneratorSettings Settings;
		FHeightFixture::FGridPtr Grid;
		FHeightFixture::FGeneratorPtr Result;
		if (!FHeightFixture::CreateSettings(Settings)
			|| !FVoxelLandM4HeightGrid::Create(
				GridOriginCm, GridOriginCm, GridCells, GridCells, GridSpacingCm, Heights, Grid)
			|| !FHeightFixture::CreateGenerator(Settings, Grid, false, false, Result))
		{
			return {};
		}
		return Result;
	}

	bool Update() override
	{
		constexpr double TimeoutSeconds = 30.0;
		constexpr float StepSeconds = 1.f / 60.f;
		if (FPlatformTime::Seconds() - Started > TimeoutSeconds)
		{
			const FVoxelLandChaosStats Stats = Terrain.GetChaosCollisionStats();
			const FVector Location = Sphere.GetComponentLocation();
			FHitResult Hit;
			const bool bTraceHits =
				World.LineTraceSingleByChannel(Hit, Location, Location - FVector(0, 0, 800), ECC_PhysicsBody,
					FCollisionQueryParams(SCENE_QUERY_STAT(VoxelLandChaosPropTest), true, Sphere.GetOwner()));
			Test.AddError(FString::Printf(
				TEXT(
					"Managed prop timed out in stage %d at %s, readiness %d, support ray %d normal Z %.2f, active bodies %d, holds %lld"),
				static_cast<int32>(Stage), *Location.ToString(),
				static_cast<int32>(Terrain.GetChaosCollisionReadiness(Consumer)), bTraceHits, Hit.ImpactNormal.Z,
				Stats.ActiveBodies, static_cast<long long>(Stats.Holds)));
			return Finish(World, &Terrain);
		}

		World.Tick(LEVELTICK_All, StepSeconds);
		switch (Stage)
		{
			case EStage::AwaitFirstSupport:
				return CheckFirstSupport();
			case EStage::Recover:
				return CheckRecovery();
			case EStage::Resume:
				return CheckResume();
		}
		return false;
	}

	static bool Finish(UWorld& World, AVoxelLandWorld* Terrain)
	{
		if (Terrain)
		{
			Terrain->Shutdown();
		}
		World.EndPlay(EEndPlayReason::Destroyed);
		World.DestroyWorld(false);
		GEngine->DestroyWorldContext(&World);
		World.RemoveFromRoot();
		return true;
	}

private:
	enum class EStage : uint8
	{
		AwaitFirstSupport,
		Recover,
		Resume
	};

	bool CheckFirstSupport()
	{
		if (!Sphere.IsSimulatingPhysics())
		{
			Test.TestTrue(
				TEXT("Pending cook keeps the prop at its original height"), Sphere.GetComponentLocation().Z >= 1499.0);
			return false;
		}
		Test.TestTrue(TEXT("Simulation starts with complete collision"),
			Terrain.GetChaosCollisionReadiness(Consumer) == EVoxelLandChaosReadiness::Ready);
		// Give the safety net an invalid pose below the cooked surface while collision remains ready.
		FVector Penetrating = Sphere.GetComponentLocation();
		Penetrating.Z = 1075.0;
		Sphere.SetWorldLocation(Penetrating, false, nullptr, ETeleportType::TeleportPhysics);
		Stage = EStage::Recover;
		return false;
	}

	bool CheckRecovery()
	{
		if (Terrain.GetChaosCollisionStats().Recoveries == 0 || !Sphere.IsSimulatingPhysics())
		{
			return false;
		}
		Test.TestTrue(TEXT("Bounded correction lifts the sphere clear of the field"),
			Sphere.GetComponentLocation().Z > 1140.0 && Sphere.GetComponentLocation().Z < 1190.0);
		if (!Test.TestTrue(TEXT("Disable Chaos collision"), Terrain.SetChaosTerrainCollisionEnabled(false)))
		{
			return Finish(World, &Terrain);
		}
		Test.TestFalse(TEXT("Disable holds the managed prop"), Sphere.IsSimulatingPhysics());
		if (!Test.TestTrue(TEXT("Enable Chaos collision again"), Terrain.SetChaosTerrainCollisionEnabled(true)))
		{
			return Finish(World, &Terrain);
		}
		Stage = EStage::Resume;
		return false;
	}

	bool CheckResume()
	{
		if (!Sphere.IsSimulatingPhysics())
		{
			return false;
		}
		Test.TestTrue(TEXT("Managed motion resumes with current support"),
			Terrain.GetChaosCollisionReadiness(Consumer) == EVoxelLandChaosReadiness::Ready
				&& Sphere.GetComponentLocation().Z > 1100.0);
		Terrain.UnregisterChaosProp(Consumer);
		CheckExpiredRegistrationCap();
		return Finish(World, &Terrain);
	}

	void CheckExpiredRegistrationCap()
	{
		TArray<TWeakObjectPtr<USphereComponent>, TInlineAllocator<16>> Registered;
		for (int32 Index = 0; Index < 16; ++Index)
		{
			USphereComponent* Temp = SpawnChaosManagedSphere(World, FVector(2900.0, 1600.0, 5000.0 + Index * 100.0));
			FGuid TempConsumer;
			if (!Test.TestTrue(TEXT("Fill the managed-prop registration cap"),
					Temp && Terrain.RegisterChaosProp(*Temp, TempConsumer, false)))
			{
				return;
			}
			Registered.Add(Temp);
		}

		FGuid Replacement;
		Test.TestFalse(
			TEXT("The full cap rejects another live prop"), Terrain.RegisterChaosProp(Sphere, Replacement, false));
		if (!Test.TestTrue(
				TEXT("Disable collision while the cap is full"), Terrain.SetChaosTerrainCollisionEnabled(false)))
		{
			return;
		}
		for (TWeakObjectPtr<USphereComponent>& Entry : Registered)
		{
			if (USphereComponent* Temp = Entry.Get())
			{
				Temp->DestroyComponent();
			}
			Test.TestFalse(TEXT("Destroyed components expire before another physics tick"), Entry.IsValid());
		}
		if (!Test.TestTrue(TEXT("Re-enable collision without another physics tick"),
				Terrain.SetChaosTerrainCollisionEnabled(true)))
		{
			return;
		}
		if (Test.TestTrue(TEXT("Expired props free slots before registration checks the cap"),
				Terrain.RegisterChaosProp(Sphere, Replacement, false)))
		{
			Terrain.UnregisterChaosProp(Replacement);
		}
	}

	FAutomationTestBase& Test;
	UWorld& World;
	AVoxelLandWorld& Terrain;
	USphereComponent& Sphere;
	FGuid Consumer;
	const double Started = FPlatformTime::Seconds();
	EStage Stage = EStage::AwaitFirstSupport;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandChaosManagedPropTest, "VoxelLand.Collision.Chaos.ManagedPropRecovery",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandChaosManagedPropTest::RunTest(const FString& Parameters)
{
	using FCommand = FVoxelLandChaosManagedPropCommand;
	if (!TestNotNull(TEXT("Engine available"), GEngine))
	{
		return false;
	}
	if (GetDefault<UPhysicsSettings>()->bTickPhysicsAsync)
	{
		AddWarning(TEXT("Managed Chaos props require synchronous physics"));
		return true;
	}
	FCommand::FHeightFixture::FGeneratorPtr Generator =
		Async(EAsyncExecution::Thread, &FCommand::MakeFlatGenerator).Get();
	if (!TestTrue(TEXT("Create the flat completed terrain field"), Generator.IsValid()))
	{
		return false;
	}
	const UWorld::InitializationValues Values = UWorld::InitializationValues()
													.CreatePhysicsScene(true)
													.ShouldSimulatePhysics(true)
													.EnableTraceCollision(true)
													.CreateNavigation(false)
													.CreateAISystem(false);
	UWorld* World =
		UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
	if (!TestNotNull(TEXT("Game world created"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	AVoxelLandWorld* Terrain = World->SpawnActor<AVoxelLandWorld>();
	USphereComponent* Sphere = SpawnChaosManagedSphere(*World, FVector(2900.0, 1600.0, 1500.0));
	if (!TestNotNull(TEXT("Terrain actor created"), Terrain) || !TestNotNull(TEXT("Prop sphere created"), Sphere))
	{
		FCommand::Finish(*World, Terrain);
		return false;
	}
	World->GetWorldSettings()->NotifyBeginPlay();
	Terrain->WorkerCount = 1;
	Terrain->bDataOnly = true;
	Terrain->Initialize(MoveTemp(Generator));
	if (!TestTrue(TEXT("Enable Chaos explicitly"), Terrain->SetChaosTerrainCollisionEnabled(true)))
	{
		FCommand::Finish(*World, Terrain);
		return false;
	}
	TestFalse(TEXT("The prop starts paused"), Sphere->IsSimulatingPhysics());
	FGuid Consumer;
	if (!TestTrue(TEXT("Register the prop"), Terrain->RegisterChaosProp(*Sphere, Consumer)))
	{
		FCommand::Finish(*World, Terrain);
		return false;
	}
	TestFalse(TEXT("Registration suspends until the floor is cooked"), Sphere->IsSimulatingPhysics());
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(*this, *World, *Terrain, *Sphere, Consumer));
	return true;
}

#endif
