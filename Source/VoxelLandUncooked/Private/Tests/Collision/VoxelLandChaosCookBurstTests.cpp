#if WITH_DEV_AUTOMATION_TESTS

#include "../Clipmap/Planning/VoxelLandCheckpointTestFixture.h"
#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "World/Collision/Chaos/VoxelLandChaosCook.h"
#include "World/Collision/Chaos/VoxelLandChaosStats.h"
#include "World/Collision/Chaos/VoxelLandChaosTerrainComponent.h"
#include "World/Collision/Chaos/VoxelLandChaosTerrainComponentHost.h"
#include "World/VoxelLandWorld.h"

class FVoxelLandChaosCookBurstCommand final : public IAutomationLatentCommand
{
public:
	using FHeightFixture = FVoxelLandM7CheckpointTestFixture;

	FVoxelLandChaosCookBurstCommand(FAutomationTestBase& InTest, UWorld& InWorld, AVoxelLandWorld& InTerrain)
		: Test(InTest)
		, World(InWorld)
		, Terrain(InTerrain)
	{
		for (int32 Y = -TileRadius; Y <= TileRadius; ++Y)
		{
			for (int32 X = -TileRadius; X <= TileRadius; ++X)
			{
				Tiles.Add({FIntVector(X, Y, 0), 0});
			}
		}
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
		if (FPlatformTime::Seconds() - Started > 35.0)
		{
			Test.AddError(
				FString::Printf(TEXT("Chaos burst timed out at phase %d with %d submissions and %d live bodies"),
					static_cast<int32>(Phase), *Submitted, PreviousBodies.Num()));
			return Finish();
		}
		World.Tick(LEVELTICK_All, 1.0f / 60.0f);
		if (Phase == EPhase::Start)
		{
			return Start();
		}

		const FVoxelLandChaosStats Stats = Terrain.GetChaosCollisionStats();
		if (!CheckTick(Stats))
		{
			return Finish();
		}
		switch (Phase)
		{
			case EPhase::Initial:
				return FinishInitial(Stats);
			case EPhase::Rebuild:
				return FinishRebuild(Stats);
			case EPhase::Retire:
				return PreviousBodies.IsEmpty() && Stats.ResidentBytes == 0 ? Finish() : false;
			case EPhase::Start:
				break;
		}
		return Finish();
	}

private:
	enum class EPhase : uint8
	{
		Start,
		Initial,
		Rebuild,
		Retire
	};

	using FBodyKey = TWeakObjectPtr<UVoxelLandChaosTerrainComponent>;
	static constexpr int32 TileRadius = 2;
	static constexpr int32 FloorHeightCm = 1100;
	static constexpr double TileWidthCm = 3200.0;
	static constexpr int32 MaxWorkers = 4;
	static constexpr int32 MaxCooks = 4;
	static constexpr int32 MaxUpdates = 2;

	bool Start()
	{
		FVoxelLandChaosCollisionSettings Settings;
		Settings.bEnabled = true;
		Settings.MaxMeshWorkers = MaxWorkers;
		Settings.MaxConcurrentCooks = MaxCooks;
		Settings.MaxPhysicsUpdatesPerTick = MaxUpdates;
		Settings.RetirementGraceSeconds = 0.0;
		FString Diagnostic;
		if (!Terrain.ConfigureChaosTerrainCollision(Settings, Diagnostic))
		{
			Test.AddError(Diagnostic);
			return Finish();
		}
		const TSharedRef<int32, ESPMode::ThreadSafe> Count = Submitted;
		Terrain.SetChaosCookHookForTests([Count](TSharedRef<const FVoxelLandChaosMesh, ESPMode::ThreadSafe> Mesh,
											 FVoxelLandChaosCook::FCompletion Completion) {
			++*Count;
			return FVoxelLandChaosCook::Start(Mesh, MoveTemp(Completion));
		});
		TRACE_BOOKMARK(TEXT("VoxelLand Chaos cook burst demand"));
		if (!Test.TestEqual(TEXT("A 25-tile lease starts pending"), Terrain.UpdateChaosCollisionTiles(Owner, Tiles),
				EVoxelLandChaosReadiness::Pending))
		{
			return Finish();
		}
		Phase = EPhase::Initial;
		return false;
	}

	bool CheckTick(const FVoxelLandChaosStats& Stats)
	{
		TSet<FBodyKey> Bodies;
		for (TActorIterator<AVoxelLandChaosTerrainComponentHost> Host(&World); Host; ++Host)
		{
			TArray<UVoxelLandChaosTerrainComponent*> Components;
			Host->GetComponents(Components);
			for (UVoxelLandChaosTerrainComponent* Component : Components)
			{
				if (IsValid(Component) && Component->IsRegistered() && Component->IsPhysicsStateCreated())
				{
					Bodies.Add(FBodyKey(Component));
				}
			}
		}
		int32 Installed = 0;
		for (const FBodyKey& Body : Bodies)
		{
			Installed += PreviousBodies.Contains(Body) ? 0 : 1;
		}
		const int32 Removed = PreviousBodies.Num() - Bodies.Num() + Installed;
		PeakInstalls = FMath::Max(PeakInstalls, Installed);
		PeakRetirements = FMath::Max(PeakRetirements, Removed);
		PeakCooks = FMath::Max(PeakCooks, Stats.CookActive);
		PreviousBodies = MoveTemp(Bodies);

		if (Installed > MaxUpdates || Removed > MaxUpdates || Stats.MeshActive > MaxWorkers
			|| Stats.CookActive > MaxCooks || Stats.Failures != 0)
		{
			Test.AddError(FString::Printf(
				TEXT("Burst budget failed with %d installed, %d retired, %d mesh workers, %d cooks and %lld failures"),
				Installed, Removed, Stats.MeshActive, Stats.CookActive, Stats.Failures));
			return false;
		}
		if (Phase == EPhase::Rebuild)
		{
			RebuildInstalls += Installed;
			return Test.TestEqual(
				TEXT("Rebuild preserves full installed coverage on every tick"), PreviousBodies.Num(), Tiles.Num());
		}
		return true;
	}

	bool FinishInitial(const FVoxelLandChaosStats& Stats)
	{
		if (Terrain.GetChaosCollisionReadiness(Owner) != EVoxelLandChaosReadiness::Ready)
		{
			return false;
		}
		TRACE_BOOKMARK(TEXT("VoxelLand Chaos cook burst settled"));
		if (!Test.TestEqual(TEXT("Every requested collider installed"), PreviousBodies.Num(), Tiles.Num())
			|| !Test.TestEqual(TEXT("The first burst submitted one cook per tile"), *Submitted, Tiles.Num())
			|| !Test.TestEqual(TEXT("The initial cook queue drained"), Stats.CookActive, 0) || !TraceFootprint()
			|| !Test.TestTrue(
				TEXT("Request one rebuild for all settled tiles"), Terrain.RequestChaosCollisionRebuild(Tiles)))
		{
			return Finish();
		}
		TRACE_BOOKMARK(TEXT("VoxelLand Chaos cook burst rebuild"));
		Phase = EPhase::Rebuild;
		return false;
	}

	bool FinishRebuild(const FVoxelLandChaosStats& Stats)
	{
		if (RebuildInstalls < Tiles.Num())
		{
			return false;
		}
		TRACE_BOOKMARK(TEXT("VoxelLand Chaos cook burst rebuilt"));
		if (!Test.TestEqual(TEXT("Each collider was rebuilt exactly once"), RebuildInstalls, Tiles.Num())
			|| !Test.TestEqual(TEXT("The replacement cook count is exact"), *Submitted, 2 * Tiles.Num())
			|| !Test.TestEqual(TEXT("All rebuild cooks drained"), Stats.CookActive, 0)
			|| !Test.TestEqual(TEXT("The full footprint is ready after rebuild"),
				Terrain.GetChaosCollisionReadiness(Owner), EVoxelLandChaosReadiness::Ready)
			|| !TraceFootprint())
		{
			return Finish();
		}
		Terrain.ReleaseChaosCollisionLease(Owner);
		TRACE_BOOKMARK(TEXT("VoxelLand Chaos cook burst retirement"));
		Phase = EPhase::Retire;
		return false;
	}

	bool TraceFootprint() const
	{
		for (const FVoxelLandChunkKey& Key : Tiles)
		{
			const double X = (Key.Coordinate.X + 0.5) * TileWidthCm;
			const double Y = (Key.Coordinate.Y + 0.5) * TileWidthCm;
			FHitResult Hit;
			const bool bHit = World.LineTraceSingleByChannel(
				Hit, FVector(X, Y, FloorHeightCm + 300.0), FVector(X, Y, FloorHeightCm - 300.0), ECC_Pawn);
			if (!bHit || !Cast<UVoxelLandChaosTerrainComponent>(Hit.GetComponent())
				|| !FMath::IsNearlyEqual(Hit.ImpactPoint.Z, double(FloorHeightCm), 2.0))
			{
				Test.AddError(
					FString::Printf(TEXT("Pawn trace missed the installed floor for %s"), *Key.Coordinate.ToString()));
				return false;
			}
		}
		return true;
	}

	bool Finish()
	{
		TRACE_BOOKMARK(TEXT("VoxelLand Chaos cook burst finish"));
		Test.AddInfo(FString::Printf(
			TEXT(
				"Chaos burst: tiles=%d submitted=%d replacements=%d peak cooks=%d peak installs/tick=%d peak retires/tick=%d"),
			Tiles.Num(), *Submitted, RebuildInstalls, PeakCooks, PeakInstalls, PeakRetirements));
		Terrain.SetChaosCookHookForTests({});
		Terrain.ReleaseChaosCollisionLease(Owner);
		Terrain.Shutdown();
		Test.TestEqual(
			TEXT("Shutdown leaves no active Chaos bodies"), Terrain.GetChaosCollisionStats().ActiveBodies, 0);
		World.DestroyWorld(false);
		GEngine->DestroyWorldContext(&World);
		World.RemoveFromRoot();
		return true;
	}

	FAutomationTestBase& Test;
	UWorld& World;
	AVoxelLandWorld& Terrain;
	TArray<FVoxelLandChunkKey> Tiles;
	TSet<FBodyKey> PreviousBodies;
	const FGuid Owner = FGuid::NewGuid();
	const double Started = FPlatformTime::Seconds();
	const TSharedRef<int32, ESPMode::ThreadSafe> Submitted = MakeShared<int32, ESPMode::ThreadSafe>(0);
	int32 RebuildInstalls = 0;
	int32 PeakCooks = 0;
	int32 PeakInstalls = 0;
	int32 PeakRetirements = 0;
	EPhase Phase = EPhase::Start;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandChaosCookBurstTest, "VoxelLand.Collision.Chaos.CookBurst",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandChaosCookBurstTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Engine available"), GEngine))
	{
		return false;
	}
	const FVoxelLandChaosCookBurstCommand::FHeightFixture::FGeneratorPtr Generator =
		Async(EAsyncExecution::Thread, &FVoxelLandChaosCookBurstCommand::MakeGenerator).Get();
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
	Terrain->WorkerCount = 4;
	World->GetWorldSettings()->NotifyBeginPlay();
	Terrain->Initialize(Generator);
	ADD_LATENT_AUTOMATION_COMMAND(FVoxelLandChaosCookBurstCommand(*this, *World, *Terrain));
	return true;
}

#endif
