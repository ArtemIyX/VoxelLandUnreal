#if WITH_DEV_AUTOMATION_TESTS

#include "../Clipmap/Planning/VoxelLandCheckpointTestFixture.h"
#include "Async/Async.h"
#include "Components/PrimitiveComponent.h"
#include "Data/Meshing/VoxelLandChaosMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Generation/Meshing/VoxelLandChaosMeshing.h"
#include "Misc/AutomationTest.h"
#include "World/Collision/Chaos/VoxelLandChaosCook.h"
#include "World/Collision/Chaos/VoxelLandChaosStats.h"
#include "World/VoxelLandWorld.h"

class FVoxelLandChaosFieldChangesCommand final : public IAutomationLatentCommand
{
public:
	using FHeightFixture = FVoxelLandM7CheckpointTestFixture;
	static constexpr int32 OriginalHeightCm = 1100;
	static constexpr int32 ReplacementHeightCm = 1700;

	FVoxelLandChaosFieldChangesCommand(FAutomationTestBase& InTest, UWorld& InWorld, AVoxelLandWorld& InTerrain,
		FHeightFixture::FChunkPtr InReplacementField, const int64 InSingleTileBudget)
		: Test(InTest)
		, World(InWorld)
		, Terrain(InTerrain)
		, ReplacementField(MoveTemp(InReplacementField))
		, SingleTileBudget(InSingleTileBudget)
	{
	}

	static FHeightFixture::FGeneratorPtr MakeGenerator(const int32 HeightCm)
	{
		constexpr int32 Cells = 80;
		TArray<int32> Heights;
		Heights.Init(HeightCm * 256, (Cells + 1) * (Cells + 1));
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

	static FHeightFixture::FChunkPtr BuildField(const FHeightFixture::FGeneratorPtr& Generator)
	{
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		FHeightFixture::FChunkPtr Chunk;
		if (Generator.IsValid() && !Generator->CreateWorker(Worker)
			&& !Generator->GenerateChunk(FVoxelLandChunkKey{}, *Worker, Chunk))
		{
			return Chunk;
		}
		return {};
	}

	static int64 BuildSingleTileBudget(const FHeightFixture::FGeneratorPtr& Generator)
	{
		const FHeightFixture::FChunkPtr Chunk = BuildField(Generator);
		FVoxelLandChaosMesh Mesh;
		if (!Chunk.IsValid()
			|| FVoxelLandChaosMeshing::Build(*Chunk, FHeightFixture::VoxelSizeCm, 0, Mesh, &FHeightFixture::NeverCancel)
				   != EVoxelLandMeshResult::Success
			|| Mesh.Indices.IsEmpty())
		{
			return 0;
		}
		// Match the pipeline's resident cook estimate so exactly one installed tile fits.
		constexpr int64 CookEstimateMultiplier = 4;
		return CookEstimateMultiplier * (int64(Mesh.Vertices.GetAllocatedSize()) + Mesh.Indices.GetAllocatedSize());
	}

	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 20.0)
		{
			Test.AddError(FString::Printf(TEXT("Chaos field changes timed out at phase %d after %d cook submissions"),
				static_cast<int32>(Phase), *Submissions));
			return Finish();
		}
		World.Tick(LEVELTICK_All, 1.0f / 60.0f);
		switch (Phase)
		{
			case EPhase::Start:
				return Start();
			case EPhase::CheckFailedField:
				return CheckFailedField();
			case EPhase::WaitOriginal:
				return WaitOriginal();
			case EPhase::WaitBudgetRebuild:
				return WaitBudgetRebuild();
			case EPhase::WaitNewField:
				return WaitNewField();
			case EPhase::CheckStaleRevision:
				return CheckStaleRevision();
			case EPhase::WaitDisabledDrain:
				return WaitDisabledDrain();
			case EPhase::CheckReenabledPolicy:
				return CheckReenabledPolicy();
		}
		return Finish();
	}

private:
	enum class EPhase : uint8
	{
		Start,
		CheckFailedField,
		WaitOriginal,
		WaitBudgetRebuild,
		WaitNewField,
		CheckStaleRevision,
		WaitDisabledDrain,
		CheckReenabledPolicy
	};

	TConstArrayView<FVoxelLandChunkKey> Tiles() const { return MakeArrayView(&Tile, 1); }

	bool Start()
	{
		FVoxelLandChaosCollisionSettings Settings;
		Settings.bEnabled = true;
		Settings.MaxCookedBytes = SingleTileBudget;
		Settings.RetirementGraceSeconds = 0.0;
		FString Diagnostic;
		if (!Terrain.ConfigureChaosTerrainCollision(Settings, Diagnostic))
		{
			Test.AddError(Diagnostic);
			return Finish();
		}
		const TSharedRef<int32, ESPMode::ThreadSafe> Count = Submissions;
		Terrain.SetChaosCookHookForTests([Count](TSharedRef<const FVoxelLandChaosMesh, ESPMode::ThreadSafe> Mesh,
											 FVoxelLandChaosCook::FCompletion Completion) {
			++*Count;
			return FVoxelLandChaosCook::Start(Mesh, MoveTemp(Completion));
		});
		const FVoxelLandChunkKey FailingTile{FIntVector(1, 0, 0), 0};
		if (!Test.TestEqual(TEXT("The failure lease starts pending"),
				Terrain.UpdateChaosCollisionTiles(FailedOwner, MakeArrayView(&FailingTile, 1)),
				EVoxelLandChaosReadiness::Pending)
			|| !Test.TestTrue(
				TEXT("Exhaust field attempts before dispatch"), Terrain.FailChaosFieldForTests(FailingTile)))
		{
			return Finish();
		}
		Phase = EPhase::CheckFailedField;
		return false;
	}

	bool CheckFailedField()
	{
		const FVoxelLandChaosStats Stats = Terrain.GetChaosCollisionStats();
		if (!Test.TestEqual(TEXT("Exhausted terrain data fails Chaos readiness"),
				Terrain.GetChaosCollisionReadiness(FailedOwner), EVoxelLandChaosReadiness::Failed)
			|| !Test.TestEqual(TEXT("A failed field creates no mesh jobs"), Stats.MeshActive, 0)
			|| !Test.TestEqual(TEXT("A failed field never submits a cook"), *Submissions, 0)
			|| !Test.TestEqual(TEXT("A failed field creates no Chaos body"), Stats.ActiveBodies, 0))
		{
			return Finish();
		}
		Terrain.ReleaseChaosCollisionLease(FailedOwner);
		if (!Test.TestEqual(TEXT("The valid field starts pending"), Terrain.UpdateChaosCollisionTiles(Owner, Tiles()),
				EVoxelLandChaosReadiness::Pending))
		{
			return Finish();
		}
		Phase = EPhase::WaitOriginal;
		return false;
	}

	bool WaitOriginal()
	{
		if (Terrain.GetChaosCollisionReadiness(Owner) != EVoxelLandChaosReadiness::Ready)
		{
			return false;
		}
		FHitResult Hit;
		if (!Test.TestTrue(TEXT("The original field produced real Pawn contact"), TraceAtHeight(OriginalHeightCm, Hit))
			|| !Test.TestEqual(TEXT("One collider fills the entire resident budget"),
				Terrain.GetChaosCollisionStats().ResidentBytes, SingleTileBudget)
			|| !Test.TestTrue(
				TEXT("Request a recook with no resident headroom"), Terrain.RequestChaosCollisionRebuild(Tiles())))
		{
			return Finish();
		}
		PreviousBody = Hit.GetComponent();
		Phase = EPhase::WaitBudgetRebuild;
		return false;
	}

	bool WaitBudgetRebuild()
	{
		FHitResult Hit;
		if (!Test.TestTrue(TEXT("Budget-limited rebuild keeps existing contact"), TraceAtHeight(OriginalHeightCm, Hit))
			|| !Test.TestEqual(TEXT("The unchanged terrain remains ready while its cook is pending"),
				Terrain.GetChaosCollisionReadiness(Owner), EVoxelLandChaosReadiness::Ready))
		{
			return Finish();
		}
		if (Hit.GetComponent() == PreviousBody.Get())
		{
			return false;
		}
		if (!Test.TestEqual(TEXT("The full-budget replacement used exactly two cooks"), *Submissions, 2)
			|| !Test.TestEqual(TEXT("The replacement still fits one tile"),
				Terrain.GetChaosCollisionStats().ResidentBytes, SingleTileBudget)
			|| !Test.TestTrue(TEXT("Publish a newer completed field for the same tile"),
				Terrain.ReplaceChaosFieldForTests(ReplacementField))
			|| !Test.TestTrue(
				TEXT("Notify Chaos of the newer field revision"), Terrain.NotifyChaosCollisionSourceChanged(Tiles(), 2))
			|| !Test.TestEqual(TEXT("Changed geometry invalidates readiness during rebuild"),
				Terrain.GetChaosCollisionReadiness(Owner), EVoxelLandChaosReadiness::Pending))
		{
			return Finish();
		}
		Phase = EPhase::WaitNewField;
		return false;
	}

	bool WaitNewField()
	{
		if (Terrain.GetChaosCollisionReadiness(Owner) != EVoxelLandChaosReadiness::Ready)
		{
			return false;
		}
		FHitResult Hit;
		if (!Test.TestTrue(
				TEXT("The rebuilt body traces against the replacement height"), TraceAtHeight(ReplacementHeightCm, Hit))
			|| !Test.TestEqual(TEXT("Field replacement submitted its own cook"), *Submissions, 3)
			|| !Test.TestFalse(TEXT("An older source notification is rejected"),
				Terrain.NotifyChaosCollisionSourceChanged(Tiles(), 1)))
		{
			return Finish();
		}
		PreviousBody = Hit.GetComponent();
		Phase = EPhase::CheckStaleRevision;
		return false;
	}

	bool CheckStaleRevision()
	{
		FHitResult Hit;
		Test.TestTrue(TEXT("A stale notification cannot replace the current body"),
			TraceAtHeight(ReplacementHeightCm, Hit) && Hit.GetComponent() == PreviousBody.Get());
		Test.TestEqual(TEXT("Rejection did not queue a cook"), *Submissions, 3);
		Test.TestEqual(TEXT("Rejection did not invalidate readiness"), Terrain.GetChaosCollisionReadiness(Owner),
			EVoxelLandChaosReadiness::Ready);
		FVoxelLandChaosCollisionOverride Disabled;
		Disabled.Mode = EVoxelLandChaosCollisionOverrideMode::Disabled;
		if (!Test.TestTrue(TEXT("Accept a revisioned tile disable before global deactivation"),
				Terrain.ApplyChaosCollisionOverrideRevision(Tiles(), Disabled, 3))
			|| !Test.TestTrue(TEXT("Turn the world feature off"), Terrain.SetChaosTerrainCollisionEnabled(false)))
		{
			return Finish();
		}
		Phase = EPhase::WaitDisabledDrain;
		return false;
	}

	bool WaitDisabledDrain()
	{
		const FVoxelLandChaosStats Stats = Terrain.GetChaosCollisionStats();
		FHitResult Hit;
		if (Stats.MeshActive || Stats.CookActive || Stats.ApplyQueued || Stats.PendingBytes || Stats.ResidentBytes
			|| Stats.ActiveBodies || TraceSurface(Hit))
		{
			DrainedTicks = 0;
			return false;
		}
		if (++DrainedTicks < 2)
		{
			return false;
		}
		const FVoxelLandChaosCollisionOverride Inherit;
		if (!Test.TestTrue(
				TEXT("Re-enable after all disabled work drains"), Terrain.SetChaosTerrainCollisionEnabled(true))
			|| !Test.TestFalse(TEXT("An older policy cannot clear the retained override"),
				Terrain.ApplyChaosCollisionOverrideRevision(Tiles(), Inherit, 2))
			|| !Test.TestFalse(TEXT("An older source revision is still rejected after re-enable"),
				Terrain.NotifyChaosCollisionSourceChanged(Tiles(), 1))
			|| !Test.TestEqual(TEXT("The tile override survives global disable and re-enable"),
				Terrain.UpdateChaosCollisionTiles(Owner, Tiles()), EVoxelLandChaosReadiness::Disabled))
		{
			return Finish();
		}
		Phase = EPhase::CheckReenabledPolicy;
		return false;
	}

	bool CheckReenabledPolicy()
	{
		FHitResult Hit;
		const FVoxelLandChaosStats Stats = Terrain.GetChaosCollisionStats();
		Test.TestFalse(TEXT("Re-enabling the world does not publish the disabled tile"), TraceSurface(Hit));
		Test.TestEqual(TEXT("The retained disable starts no mesh work"), Stats.MeshActive, 0);
		Test.TestEqual(TEXT("The retained disable starts no cook"), Stats.CookActive, 0);
		return Finish();
	}

	bool TraceSurface(FHitResult& Hit) const
	{
		return World.LineTraceSingleByChannel(
			Hit, FVector(1600.0, 1600.0, 2300.0), FVector(1600.0, 1600.0, 500.0), ECC_Pawn);
	}

	bool TraceAtHeight(const int32 HeightCm, FHitResult& Hit) const
	{
		return TraceSurface(Hit) && FMath::IsNearlyEqual(Hit.ImpactPoint.Z, double(HeightCm), 2.0);
	}

	bool Finish()
	{
		Terrain.SetChaosCookHookForTests({});
		Terrain.ReleaseChaosCollisionLease(FailedOwner);
		Terrain.ReleaseChaosCollisionLease(Owner);
		Terrain.Shutdown();
		World.DestroyWorld(false);
		GEngine->DestroyWorldContext(&World);
		World.RemoveFromRoot();
		return true;
	}

	FAutomationTestBase& Test;
	UWorld& World;
	AVoxelLandWorld& Terrain;
	const FHeightFixture::FChunkPtr ReplacementField;
	const int64 SingleTileBudget;
	const FVoxelLandChunkKey Tile;
	const FGuid Owner = FGuid::NewGuid();
	const FGuid FailedOwner = FGuid::NewGuid();
	const double Started = FPlatformTime::Seconds();
	const TSharedRef<int32, ESPMode::ThreadSafe> Submissions = MakeShared<int32, ESPMode::ThreadSafe>(0);
	TWeakObjectPtr<UPrimitiveComponent> PreviousBody;
	int32 DrainedTicks = 0;
	EPhase Phase = EPhase::Start;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandChaosFieldChangesTest, "VoxelLand.Collision.Chaos.FieldChanges",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandChaosFieldChangesTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Engine available"), GEngine))
	{
		return false;
	}
	using FCommand = FVoxelLandChaosFieldChangesCommand;
	const FCommand::FHeightFixture::FGeneratorPtr Original =
		Async(EAsyncExecution::Thread, []() { return FCommand::MakeGenerator(FCommand::OriginalHeightCm); }).Get();
	if (!TestTrue(TEXT("Construct original height terrain"), Original.IsValid()))
	{
		return false;
	}
	const int64 Budget =
		Async(EAsyncExecution::Thread, [Original]() { return FCommand::BuildSingleTileBudget(Original); }).Get();
	const FCommand::FHeightFixture::FChunkPtr Changed = Async(EAsyncExecution::Thread, []() {
		return FCommand::BuildField(FCommand::MakeGenerator(FCommand::ReplacementHeightCm));
	}).Get();
	if (!TestTrue(TEXT("Measure one tile of cook residency"), Budget > 0)
		|| !TestTrue(TEXT("Prepare a newer completed snapshot"), Changed.IsValid()))
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
	Terrain->Initialize(Original);
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(*this, *World, *Terrain, Changed, Budget));
	return true;
}

#endif
