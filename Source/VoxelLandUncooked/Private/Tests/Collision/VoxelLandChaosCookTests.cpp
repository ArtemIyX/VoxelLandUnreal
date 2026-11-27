#if WITH_DEV_AUTOMATION_TESTS
#include "Components/SphereComponent.h"
#include "Data/Meshing/VoxelLandChaosMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/BodySetup.h"
#include "World/Collision/Chaos/VoxelLandChaosCook.h"
#include "World/Collision/Chaos/VoxelLandChaosCookBodySettings.h"
#include "World/Collision/Chaos/VoxelLandChaosTerrainComponent.h"
#include "World/Collision/Chaos/VoxelLandChaosTerrainComponentHost.h"

struct FVoxelLandChaosCookTestOutcome
{
	FVoxelLandChaosCookResultPtr Result;
	int32 CallbackCount = 0;
};

class FVoxelLandChaosCookTestCommand final : public IAutomationLatentCommand
{
public:
	FVoxelLandChaosCookTestCommand(
		FAutomationTestBase& InTest, UWorld& InWorld, AVoxelLandChaosTerrainComponentHost& InHost)
		: Test(InTest)
		, World(&InWorld)
		, Host(&InHost)
	{
	}

	[[nodiscard]] static TSharedRef<const FVoxelLandChaosMesh, ESPMode::ThreadSafe> MakeFloor();
	[[nodiscard]] static bool SubmitFloor(TSharedRef<FVoxelLandChaosCookTestOutcome, ESPMode::ThreadSafe> Result);
	void CloseWorld();
	virtual bool Update() override;

	TSharedRef<FVoxelLandChaosCookTestOutcome, ESPMode::ThreadSafe> Initial =
		MakeShared<FVoxelLandChaosCookTestOutcome, ESPMode::ThreadSafe>();

private:
	[[nodiscard]] bool InstallAndDrop();
	[[nodiscard]] bool AdvancePhysics();
	[[nodiscard]] bool CheckDetached();
	[[nodiscard]] bool TraceFloor(const FVector& Position, FHitResult& Out) const;
	[[nodiscard]] USphereComponent* SpawnProp(const FVector& Position) const;

	static constexpr double DeadlineSeconds = 15.0;
	static constexpr double HalfFloorWidth = 400.0;
	static constexpr double FloorHeight = 125.0;
	static constexpr double DropHeight = 200.0;
	static constexpr float PropRadius = 18.f;
	static constexpr float StepSeconds = 1.f / 60.f;
	static constexpr int32 StepsToSettle = 240;

	FAutomationTestBase& Test;
	UWorld* World;
	AVoxelLandChaosTerrainComponentHost* Host;
	TWeakObjectPtr<UVoxelLandChaosTerrainComponent> Floor;
	TWeakObjectPtr<USphereComponent> Prop;
	const TSharedRef<FVoxelLandChaosCookTestOutcome, ESPMode::ThreadSafe> Detached =
		MakeShared<FVoxelLandChaosCookTestOutcome, ESPMode::ThreadSafe>();
	const double StartedAt = FPlatformTime::Seconds();
	double LowestPropHeight = TNumericLimits<double>::Max();
	int32 Steps = 0;
	bool bInstalled = false;
};

TSharedRef<const FVoxelLandChaosMesh, ESPMode::ThreadSafe> FVoxelLandChaosCookTestCommand::MakeFloor()
{
	const TSharedRef<FVoxelLandChaosMesh, ESPMode::ThreadSafe> Mesh =
		MakeShared<FVoxelLandChaosMesh, ESPMode::ThreadSafe>();
	const float Edge = static_cast<float>(HalfFloorWidth);
	Mesh->Vertices = {{-Edge, -Edge, 0.f}, {Edge, -Edge, 0.f}, {Edge, Edge, 0.f}, {-Edge, Edge, 0.f}};
	Mesh->Indices = {0, 1, 2, 0, 2, 3};
	Mesh->Bounds = FBox3f(Mesh->Vertices);
	return Mesh;
}

bool FVoxelLandChaosCookTestCommand::SubmitFloor(TSharedRef<FVoxelLandChaosCookTestOutcome, ESPMode::ThreadSafe> Result)
{
	return FVoxelLandChaosCook::Start(MakeFloor(), [Result = MoveTemp(Result)](FVoxelLandChaosCookResultPtr Cooked) {
		check(IsInGameThread());
		++Result->CallbackCount;
		Result->Result = MoveTemp(Cooked);
	});
}

void FVoxelLandChaosCookTestCommand::CloseWorld()
{
	if (World)
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		World = nullptr;
		Host = nullptr;
	}
}

bool FVoxelLandChaosCookTestCommand::Update()
{
	if (FPlatformTime::Seconds() - StartedAt > DeadlineSeconds)
	{
		Test.AddError(TEXT("Timed out waiting for UE Chaos terrain cook or real physics"));
		CloseWorld();
		return true;
	}
	if (!World)
	{
		return CheckDetached();
	}
	if (Initial->CallbackCount == 0)
	{
		return false;
	}
	if (!bInstalled)
	{
		bInstalled = InstallAndDrop();
		if (!bInstalled)
		{
			CloseWorld();
			return true;
		}
	}
	return AdvancePhysics();
}

bool FVoxelLandChaosCookTestCommand::InstallAndDrop()
{
	if (!Test.TestTrue(TEXT("Initial async cook finished exactly once with triangles"),
			Initial->CallbackCount == 1 && Initial->Result.IsValid()))
	{
		return false;
	}

	FHitResult Before;
	Test.TestFalse(
		TEXT("A cooked setup alone creates no active world collider"), TraceFloor(FVector::ZeroVector, Before));

	FVoxelLandChaosCookBodySettings Settings;
	Floor = UVoxelLandChaosTerrainComponent::Install(*Host, FVector(0.0, 0.0, FloorHeight), *Initial->Result, Settings);
	Initial->Result.Reset();
	const UVoxelLandChaosTerrainComponent* Component = Floor.Get();
	if (!Test.TestNotNull(TEXT("Cooked floor installed as an active body"), Component))
	{
		return false;
	}
	Test.TestEqual(TEXT("Terrain is static"), Component->Mobility.GetValue(), EComponentMobility::Static);
	Test.TestFalse(TEXT("Terrain never simulates"), Component->IsSimulatingPhysics());
	Test.TestFalse(TEXT("Terrain never generates overlap events"), Component->GetGenerateOverlapEvents());
	Test.TestEqual(TEXT("Terrain object channel"), Component->GetCollisionObjectType(), ECC_WorldStatic);

	FHitResult Hit;
	if (!Test.TestTrue(TEXT("Pawn trace hits installed terrain"), TraceFloor(FVector::ZeroVector, Hit)))
	{
		return false;
	}
	Test.TestTrue(
		TEXT("Trace lands at the static floor height"), FMath::IsNearlyEqual(Hit.ImpactPoint.Z, FloorHeight, 1.0));

	const FIntVector OriginShift(700, -500, 0);
	if (!Test.TestTrue(TEXT("World rebase succeeds"), World->SetNewWorldOrigin(OriginShift))
		|| !Test.TestTrue(TEXT("The collider shifts exactly once"), TraceFloor(-FVector(OriginShift), Hit)))
	{
		return false;
	}
	Prop = SpawnProp(-FVector(OriginShift) + FVector(0.0, 0.0, FloorHeight + DropHeight));
	return Test.TestTrue(TEXT("A convex Chaos prop starts simulating"), Prop.IsValid() && Prop->IsSimulatingPhysics());
}

bool FVoxelLandChaosCookTestCommand::AdvancePhysics()
{
	// Advance one step per engine frame so Chaos can publish each result to the game thread.
	World->Tick(LEVELTICK_All, StepSeconds);
	++Steps;
	if (Prop.IsValid())
	{
		LowestPropHeight = FMath::Min(LowestPropHeight, Prop->GetComponentLocation().Z);
	}
	if (Steps < StepsToSettle)
	{
		return false;
	}

	if (Test.TestTrue(TEXT("The prop still exists"), Prop.IsValid()))
	{
		const double Height = Prop->GetComponentLocation().Z;
		Test.TestTrue(FString::Printf(TEXT("Prop settled on the installed triangles (Z %.2f, velocity Z %.2f)"), Height,
						  Prop->GetPhysicsLinearVelocity().Z),
			FMath::IsNearlyEqual(Height, FloorHeight + PropRadius, 4.0));
		Test.TestTrue(
			TEXT("The sampled prop never passed below the collision surface"), LowestPropHeight >= FloorHeight);
	}

	const bool bSubmitted = SubmitFloor(Detached);
	Test.TestTrue(TEXT("A second cook is in flight before world teardown"), bSubmitted && Detached->CallbackCount == 0);
	Host->Destroy();
	Test.TestTrue(TEXT("Host destruction removes the published body"), !Floor.IsValid() || !Floor->IsRegistered());
	CloseWorld();
	return !bSubmitted;
}

bool FVoxelLandChaosCookTestCommand::CheckDetached()
{
	if (Detached->CallbackCount == 0)
	{
		return false;
	}
	Test.TestTrue(TEXT("Detached cook finishes once with no world and no registered collider"),
		Detached->CallbackCount == 1 && Detached->Result.IsValid() && !World
			&& (!Floor.IsValid() || !Floor->IsRegistered()));
	Detached->Result.Reset();
	return true;
}

bool FVoxelLandChaosCookTestCommand::TraceFloor(const FVector& Position, FHitResult& Out) const
{
	const FVector Start(Position.X, Position.Y, FloorHeight + DropHeight);
	const FVector End(Position.X, Position.Y, FloorHeight - DropHeight);
	return World->LineTraceSingleByChannel(Out, Start, End, ECC_Pawn);
}

USphereComponent* FVoxelLandChaosCookTestCommand::SpawnProp(const FVector& Position) const
{
	AActor* Actor = World->SpawnActor<AActor>();
	if (!Actor)
	{
		return nullptr;
	}
	USphereComponent* Sphere = NewObject<USphereComponent>(Actor, NAME_None, RF_Transient);
	Sphere->SetMobility(EComponentMobility::Movable);
	Sphere->InitSphereRadius(PropRadius);
	Sphere->SetCollisionObjectType(ECC_WorldDynamic);
	Sphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Sphere->SetCollisionResponseToAllChannels(ECR_Block);
	Sphere->SetWorldLocation(Position);
	Actor->SetRootComponent(Sphere);
	Actor->AddInstanceComponent(Sphere);
	Sphere->RegisterComponentWithWorld(World);
	Sphere->SetSimulatePhysics(true);
	return Sphere;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandChaosCookTest, "VoxelLand.Collision.Chaos.AsyncCook",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandChaosCookTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Game engine available"), GEngine))
	{
		return false;
	}

	const UWorld::InitializationValues Init = UWorld::InitializationValues()
												  .CreatePhysicsScene(true)
												  .ShouldSimulatePhysics(true)
												  .EnableTraceCollision(true)
												  .AllowAudioPlayback(false)
												  .CreateNavigation(false);
	UWorld* World =
		UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Physics test world created"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	// This standalone test world does not have a game mode to mark play started.
	World->SetBegunPlay(true);

	AVoxelLandChaosTerrainComponentHost* Host = World->SpawnActor<AVoxelLandChaosTerrainComponentHost>();
	if (!TestNotNull(TEXT("Static terrain host spawned"), Host))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}
	const TSharedRef<FVoxelLandChaosCookTestCommand> Command =
		MakeShared<FVoxelLandChaosCookTestCommand>(*this, *World, *Host);
	if (!TestTrue(
			TEXT("Submitted a real async triangle cook"), FVoxelLandChaosCookTestCommand::SubmitFloor(Command->Initial))
		|| !TestEqual(TEXT("Real nonempty input does not complete inline"), Command->Initial->CallbackCount, 0))
	{
		Command->CloseWorld();
		return false;
	}
	FAutomationTestFramework::Get().EnqueueLatentCommand(Command);
	return true;
}
#endif
