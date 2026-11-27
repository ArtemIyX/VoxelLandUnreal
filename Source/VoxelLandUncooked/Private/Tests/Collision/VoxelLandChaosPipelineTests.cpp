#if WITH_DEV_AUTOMATION_TESTS

#include "../Clipmap/Planning/VoxelLandCheckpointTestFixture.h"
#include "Async/Async.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Generation/Collision/VoxelLandCollisionField.h"
#include "Misc/AutomationTest.h"
#include "World/Collision/VoxelLandCollisionWorldView.h"
#include "World/Collision/Chaos/VoxelLandChaosCook.h"
#include "World/Collision/Chaos/VoxelLandChaosCookHookForTests.h"
#include "World/Collision/Chaos/VoxelLandChaosStats.h"
#include "World/VoxelLandWorld.h"

class FVoxelLandChaosPipelineProbe final : public TSharedFromThis<FVoxelLandChaosPipelineProbe, ESPMode::ThreadSafe>
{
public:
	using FMesh = TSharedRef<const FVoxelLandChaosMesh, ESPMode::ThreadSafe>;
	using FCompletion = FVoxelLandChaosCook::FCompletion;

	bool Start(FMesh Mesh, FCompletion Completion)
	{
		switch (++Submissions)
		{
			case 1:
				Completion(nullptr);
				Completion(nullptr);
				return true;
			case 2:
				return false;
			case 3:
				Obsolete = MoveTemp(Completion);
				return true;
			case 4:
			{
				AwaitingDelivery = MoveTemp(Completion);
				const TSharedRef<FVoxelLandChaosPipelineProbe, ESPMode::ThreadSafe> Probe = AsShared();
				if (FVoxelLandChaosCook::Start(Mesh, [Probe](FVoxelLandChaosCookResultPtr Result) {
						Probe->RealResult = MoveTemp(Result);
						Probe->bRealCookCompleted = true;
					}))
				{
					return true;
				}
				bRealCookCompleted = true;
				return false;
			}
			case 5:
				Detached = MoveTemp(Completion);
				return true;
			default:
				return FVoxelLandChaosCook::Start(Mesh, MoveTemp(Completion));
		}
	}

	bool HoldShutdownCook(FMesh Mesh, FCompletion Completion)
	{
		ShutdownDelivery = MoveTemp(Completion);
		const TSharedRef<FVoxelLandChaosPipelineProbe, ESPMode::ThreadSafe> Probe = AsShared();
		if (FVoxelLandChaosCook::Start(Mesh, [Probe](FVoxelLandChaosCookResultPtr Result) {
				Probe->ShutdownResult = MoveTemp(Result);
				Probe->bShutdownCookCompleted = true;
			}))
		{
			return true;
		}
		bShutdownCookCompleted = true;
		return false;
	}

	int32 Submissions = 0;
	bool bRealCookCompleted = false;
	bool bShutdownCookCompleted = false;
	FCompletion Obsolete;
	FCompletion AwaitingDelivery;
	FCompletion Detached;
	FVoxelLandChaosCookResultPtr RealResult;
	FCompletion ShutdownDelivery;
	FVoxelLandChaosCookResultPtr ShutdownResult;
};

class FVoxelLandChaosPipelineCommand final : public IAutomationLatentCommand
{
public:
	using FHeightFixture = FVoxelLandM7CheckpointTestFixture;

	FVoxelLandChaosPipelineCommand(FAutomationTestBase& InTest, UWorld& InWorld, AVoxelLandWorld& InTerrain)
		: Test(InTest)
		, World(InWorld)
		, Terrain(InTerrain)
	{
	}

	static FHeightFixture::FGeneratorPtr MakeGenerator()
	{
		constexpr int32 Cells = 80;
		TArray<int32> Heights;
		Heights.Init(FloorHeightCm * 256, (Cells + 1) * (Cells + 1));
		FVoxelLandGeneratorSettings Settings;
		FHeightFixture::FGridPtr Grid;
		FHeightFixture::FGeneratorPtr Generator;
		if (FHeightFixture::CreateSettings(Settings)
			&& FVoxelLandM4HeightGrid::Create(-40000, -40000, Cells, Cells, 1000, Heights, Grid)
			&& FHeightFixture::CreateGenerator(Settings, Grid, false, false, Generator))
		{
			return Generator;
		}
		return {};
	}

	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 20.0)
		{
			Test.AddError(FString::Printf(TEXT("Chaos pipeline timed out at phase %d after %d submissions"),
				static_cast<int32>(Phase), Probe->Submissions));
			return Finish();
		}
		World.Tick(LEVELTICK_All, 1.0f / 60.0f);
		switch (Phase)
		{
			case EPhase::Start:
				return Start();
			case EPhase::WaitRetry:
				return WaitRetry();
			case EPhase::WaitNewest:
				return WaitNewest();
			case EPhase::WaitInstall:
				return WaitInstall();
			case EPhase::WaitObsolete:
				return WaitObsolete();
			case EPhase::WaitDetached:
				return WaitDetached();
			case EPhase::WaitActivated:
				return WaitActivated();
			case EPhase::WaitExpanded:
				return WaitExpanded();
			case EPhase::WaitTrimmed:
				return WaitTrimmed();
			case EPhase::WaitShutdownCook:
				return WaitShutdownCook();
			case EPhase::WaitShutdownDrain:
				return WaitShutdownDrain();
		}
		return Finish();
	}

private:
	enum class EPhase : uint8
	{
		Start,
		WaitRetry,
		WaitNewest,
		WaitInstall,
		WaitObsolete,
		WaitDetached,
		WaitActivated,
		WaitExpanded,
		WaitTrimmed,
		WaitShutdownCook,
		WaitShutdownDrain
	};

	static constexpr int32 FloorHeightCm = 1100;

	TConstArrayView<FVoxelLandChunkKey> Tiles() const { return MakeArrayView(&Tile, 1); }

	bool Start()
	{
		FVoxelLandChaosCollisionSettings Settings;
		Settings.bEnabled = true;
		Settings.RetirementGraceSeconds = 0.0;
		FString Diagnostic;
		const FVoxelLandChaosStats Disabled = Terrain.GetChaosCollisionStats();
		if (!Test.TestEqual(TEXT("Default-off lease is disabled"), Terrain.UpdateChaosCollisionTiles(Owner, Tiles()),
				EVoxelLandChaosReadiness::Disabled)
			|| !Test.TestEqual(TEXT("Default-off starts no cooks"), Disabled.CookActive, 0)
			|| !Test.TestEqual(TEXT("Default-off installs no shapes"), Disabled.ActiveBodies, 0))
		{
			return Finish();
		}
		if (!Terrain.ConfigureChaosTerrainCollision(Settings, Diagnostic))
		{
			Test.AddError(Diagnostic);
			return Finish();
		}
		const TSharedRef<FVoxelLandChaosPipelineProbe, ESPMode::ThreadSafe> CookProbe = Probe;
		Terrain.SetChaosCookHookForTests([CookProbe](FVoxelLandChaosPipelineProbe::FMesh Mesh,
											 FVoxelLandChaosPipelineProbe::FCompletion Completion) {
			return CookProbe->Start(Mesh, MoveTemp(Completion));
		});
		if (!Test.TestEqual(TEXT("The first lease waits for a cook"), Terrain.UpdateChaosCollisionTiles(Owner, Tiles()),
				EVoxelLandChaosReadiness::Pending))
		{
			return Finish();
		}
		Phase = EPhase::WaitRetry;
		return false;
	}

	bool WaitRetry()
	{
		if (!Probe->Obsolete)
		{
			return false;
		}
		const FVoxelLandChaosStats Stats = Terrain.GetChaosCollisionStats();
		if (!Test.TestEqual(
				TEXT("Inline failure and admission refusal each consume one retry"), Stats.Failures, int64(2))
			|| !Test.TestEqual(TEXT("Duplicate completion is ignored"), Stats.StaleResults, int64(1))
			|| !Test.TestEqual(TEXT("The delayed retry remains active"), Stats.CookActive, 1)
			|| !Test.TestEqual(TEXT("The delayed retry has no body"), Stats.ActiveBodies, 0)
			|| !Test.TestTrue(TEXT("A newer source invalidates the delayed retry"),
				Terrain.NotifyChaosCollisionSourceChanged(Tiles(), 2))
			|| !Test.TestFalse(
				TEXT("An older source revision is rejected"), Terrain.NotifyChaosCollisionSourceChanged(Tiles(), 1)))
		{
			return Finish();
		}
		Phase = EPhase::WaitNewest;
		return false;
	}

	bool WaitNewest()
	{
		if (!Probe->bRealCookCompleted)
		{
			return false;
		}
		const FIntVector Shift(7000, -5000, 0);
		if (!Test.TestTrue(TEXT("The latest terrain cook produced a valid result"), Probe->RealResult.IsValid())
			|| !Test.TestEqual(
				TEXT("Neither held completion may create a body"), Terrain.GetChaosCollisionStats().ActiveBodies, 0)
			|| !Test.TestTrue(TEXT("Rebase before delayed delivery"), World.SetNewWorldOrigin(Shift)))
		{
			return Finish();
		}
		Probe->AwaitingDelivery(Probe->RealResult);
		Phase = EPhase::WaitInstall;
		return false;
	}

	bool WaitInstall()
	{
		if (Terrain.GetChaosCollisionReadiness(Owner) != EVoxelLandChaosReadiness::Ready)
		{
			return false;
		}
		FHitResult Hit;
		const FVoxelLandChaosStats Stats = Terrain.GetChaosCollisionStats();
		if (!Test.TestEqual(TEXT("Only the latest cook installed a body"), Stats.ActiveBodies, 1)
			|| !Test.TestEqual(TEXT("The old cook is still counted as detached"), Stats.DetachedCooks, 1)
			|| !Test.TestTrue(TEXT("The installed body traces at the rebased location"), TraceSurface(Hit)))
		{
			return Finish();
		}
		Installed = Hit.GetComponent();
		Probe->Obsolete(Probe->RealResult);
		Phase = EPhase::WaitObsolete;
		return false;
	}

	bool WaitObsolete()
	{
		const FVoxelLandChaosStats Stats = Terrain.GetChaosCollisionStats();
		if (Stats.CookActive != 0)
		{
			return false;
		}
		FHitResult Hit;
		if (!Test.TestEqual(TEXT("Obsolete success is ignored"), Stats.StaleResults, int64(2))
			|| !Test.TestEqual(TEXT("Obsolete success leaves one body"), Stats.ActiveBodies, 1)
			|| !Test.TestTrue(TEXT("Obsolete success does not replace the installed shape"),
				TraceSurface(Hit) && Hit.GetComponent() == Installed.Get())
			|| !Test.TestTrue(
				TEXT("Queue another rebuild before deactivation"), Terrain.RequestChaosCollisionRebuild(Tiles())))
		{
			return Finish();
		}
		Phase = EPhase::WaitDetached;
		return false;
	}

	bool WaitDetached()
	{
		if (!Probe->Detached)
		{
			return false;
		}
		if (!Test.TestTrue(TEXT("Disable with a replacement in flight"), Terrain.SetChaosTerrainCollisionEnabled(false))
			|| !Test.TestEqual(TEXT("Disabled world denies the lease"), Terrain.GetChaosCollisionReadiness(Owner),
				EVoxelLandChaosReadiness::Disabled)
			|| !Test.TestTrue(
				TEXT("Re-enable before the old activation drains"), Terrain.SetChaosTerrainCollisionEnabled(true))
			|| !Test.TestEqual(TEXT("The new activation requires its own cook"),
				Terrain.UpdateChaosCollisionTiles(Owner, Tiles()), EVoxelLandChaosReadiness::Pending))
		{
			return Finish();
		}
		Probe->Detached(Probe->RealResult);
		Phase = EPhase::WaitActivated;
		return false;
	}

	bool WaitActivated()
	{
		if (Terrain.GetChaosCollisionReadiness(Owner) != EVoxelLandChaosReadiness::Ready)
		{
			return false;
		}
		const FVoxelLandChaosStats Stats = Terrain.GetChaosCollisionStats();
		FHitResult Hit;
		Test.TestEqual(TEXT("Detached activation completion is ignored"), Stats.StaleResults, int64(3));
		Test.TestEqual(TEXT("Each completion releases its cook slot once"), Stats.CookActive, 0);
		Test.TestEqual(TEXT("The new activation installs one body"), Stats.ActiveBodies, 1);
		Test.TestTrue(TEXT("The reactivated body blocks at the rebased tile"),
			TraceSurface(Hit) && Hit.GetComponent() != Installed.Get());
		const FVoxelLandChunkKey Expanded[] = {Tile, {FIntVector(1, 0, 0), 0}};
		if (!Test.TestEqual(TEXT("The expanded lease waits for the adjacent tile"),
				Terrain.UpdateChaosCollisionTiles(Owner, Expanded), EVoxelLandChaosReadiness::Pending))
		{
			return Finish();
		}
		Phase = EPhase::WaitExpanded;
		return false;
	}

	bool WaitExpanded()
	{
		if (Terrain.GetChaosCollisionReadiness(Owner) != EVoxelLandChaosReadiness::Ready)
		{
			return false;
		}
		FHitResult Hit;
		if (!Test.TestEqual(
				TEXT("The expanded lease installed both bodies"), Terrain.GetChaosCollisionStats().ActiveBodies, 2)
			|| !Test.TestTrue(TEXT("The adjacent tile blocks before trimming the lease"), TraceSurface(Hit, 4800.0)))
		{
			return Finish();
		}

		const FVoxelLandChunkKey Duplicated[] = {Tile, Tile};
		if (!Test.TestEqual(TEXT("Duplicate input replaces the lease with one ready tile"),
				Terrain.UpdateChaosCollisionTiles(Owner, Duplicated), EVoxelLandChaosReadiness::Ready))
		{
			return Finish();
		}
		FVoxelLandCollisionWorldView View;
		if (!Test.TestTrue(TEXT("Capture the trimmed lease field"),
				Terrain.CaptureChaosCollisionView(Owner, View) && View.Field.IsValid()))
		{
			return Finish();
		}
		double Density = 0.0;
		Test.TestEqual(TEXT("The kept tile remains pinned"),
			View.Field->Sample(FVector(1600.0, 1600.0, FloorHeightCm), Density),
			EVoxelLandCollisionFieldStatus::Available);
		Test.TestEqual(TEXT("The adjacent tile is absent from the trimmed lease"),
			View.Field->Sample(FVector(4800.0, 1600.0, FloorHeightCm), Density),
			EVoxelLandCollisionFieldStatus::Missing);
		Phase = EPhase::WaitTrimmed;
		return false;
	}

	bool WaitTrimmed()
	{
		FHitResult AdjacentHit;
		if (Terrain.GetChaosCollisionStats().ActiveBodies != 1 || TraceSurface(AdjacentHit, 4800.0))
		{
			return false;
		}
		FHitResult KeptHit;
		Test.TestEqual(TEXT("The normalized lease remains ready"), Terrain.GetChaosCollisionReadiness(Owner),
			EVoxelLandChaosReadiness::Ready);
		Test.TestTrue(TEXT("The kept tile still blocks after the adjacent body retires"), TraceSurface(KeptHit));

		const TSharedRef<FVoxelLandChaosPipelineProbe, ESPMode::ThreadSafe> CookProbe = Probe;
		Terrain.SetChaosCookHookForTests([CookProbe](FVoxelLandChaosPipelineProbe::FMesh Mesh,
											 FVoxelLandChaosPipelineProbe::FCompletion Completion) {
			return CookProbe->HoldShutdownCook(Mesh, MoveTemp(Completion));
		});
		if (!Test.TestTrue(TEXT("Start a real cook to detach by explicit shutdown"),
				Terrain.RequestChaosCollisionRebuild(Tiles())))
		{
			return Finish();
		}
		Phase = EPhase::WaitShutdownCook;
		return false;
	}

	bool WaitShutdownCook()
	{
		if (!Probe->bShutdownCookCompleted)
		{
			return false;
		}
		if (!Test.TestTrue(TEXT("A valid backend cook awaits delivery"), Probe->ShutdownResult.IsValid())
			|| !Test.TestEqual(TEXT("One undelivered cook is active"), Terrain.GetChaosCollisionStats().CookActive, 1))
		{
			return Finish();
		}
		Terrain.Shutdown();
		Test.TestFalse(TEXT("Explicit shutdown stops the surviving actor's tick"), Terrain.IsActorTickEnabled());
		Test.TestEqual(TEXT("Explicit shutdown keeps the detached cook accounted for"),
			Terrain.GetChaosCollisionStats().CookActive, 1);
		Probe->ShutdownDelivery(MoveTemp(Probe->ShutdownResult));
		Phase = EPhase::WaitShutdownDrain;
		return false;
	}

	bool WaitShutdownDrain()
	{
		const FVoxelLandChaosStats Stats = Terrain.GetChaosCollisionStats();
		if (Stats.CookActive || Stats.DetachedCooks || Stats.PendingBytes)
		{
			return false;
		}
		FHitResult Hit;
		Test.TestFalse(TEXT("Cook cleanup does not turn on the actor's tick"), Terrain.IsActorTickEnabled());
		Test.TestFalse(TEXT("A late successful cook cannot reinstall after shutdown"), TraceSurface(Hit));
		return Finish();
	}

	bool TraceSurface(FHitResult& Hit, const double LogicalX = 1600.0) const
	{
		const FVector Origin(World.OriginLocation);
		const FVector Start(LogicalX, 1600.0, FloorHeightCm + 300.0);
		const FVector End(LogicalX, 1600.0, FloorHeightCm - 300.0);
		return World.LineTraceSingleByChannel(Hit, Start - Origin, End - Origin, ECC_Pawn);
	}

	bool Finish()
	{
		Terrain.SetChaosCookHookForTests({});
		Terrain.ReleaseChaosCollisionLease(Owner);
		Terrain.Shutdown();
		Test.TestEqual(TEXT("Shutdown removes the collision bodies"), Terrain.GetChaosCollisionStats().ActiveBodies, 0);
		World.DestroyWorld(false);
		GEngine->DestroyWorldContext(&World);
		World.RemoveFromRoot();
		return true;
	}

	FAutomationTestBase& Test;
	UWorld& World;
	AVoxelLandWorld& Terrain;
	const FVoxelLandChunkKey Tile;
	const FGuid Owner = FGuid::NewGuid();
	const double Started = FPlatformTime::Seconds();
	const TSharedRef<FVoxelLandChaosPipelineProbe, ESPMode::ThreadSafe> Probe =
		MakeShared<FVoxelLandChaosPipelineProbe, ESPMode::ThreadSafe>();
	TWeakObjectPtr<UPrimitiveComponent> Installed;
	EPhase Phase = EPhase::Start;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandChaosPipelineTest, "VoxelLand.Collision.Chaos.Pipeline",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandChaosPipelineTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Engine available"), GEngine))
	{
		return false;
	}
	const FVoxelLandChaosPipelineCommand::FHeightFixture::FGeneratorPtr Generator =
		Async(EAsyncExecution::Thread, &FVoxelLandChaosPipelineCommand::MakeGenerator).Get();
	if (!TestTrue(TEXT("Construct deterministic height terrain"), Generator.IsValid()))
	{
		return false;
	}

	const UWorld::InitializationValues Init = UWorld::InitializationValues()
												  .CreatePhysicsScene(true)
												  .ShouldSimulatePhysics(true)
												  .EnableTraceCollision(true)
												  .CreateNavigation(false);
	UWorld* const World =
		UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Create a data-only physics world"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	AVoxelLandWorld* const Terrain = World->SpawnActor<AVoxelLandWorld>();
	if (!TestNotNull(TEXT("Create terrain"), Terrain))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}
	Terrain->bDataOnly = true;
	Terrain->WorkerCount = 1;
	World->GetWorldSettings()->NotifyBeginPlay();
	Terrain->Initialize(Generator);
	ADD_LATENT_AUTOMATION_COMMAND(FVoxelLandChaosPipelineCommand(*this, *World, *Terrain));
	return true;
}

#endif
