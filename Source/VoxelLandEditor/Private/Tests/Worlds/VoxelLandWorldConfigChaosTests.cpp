#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Graph/Sources/VoxelLandGraphOutputs.h"
#include "Data/Meshing/VoxelLandChaosMesh.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "World/Collision/Chaos/VoxelLandChaosStats.h"
#include "World/Collision/Chaos/VoxelLandChaosTerrainComponent.h"
#include "World/Collision/Chaos/VoxelLandChaosTileInfo.h"
#include "World/VoxelLandWorld.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

class FVoxelLandWorldConfigChaosCommand final : public IAutomationLatentCommand
{
public:
	FVoxelLandWorldConfigChaosCommand(
		FAutomationTestBase& InTest, UWorld& InWorld, AVoxelLandWorld& InTerrain, UVoxelLandWorldConfig& InConfig)
		: Test(InTest)
		, World(InWorld)
		, Terrain(InTerrain)
		, Config(&InConfig)
	{
	}

	static constexpr double SurfaceCm = 1150.0;

	static UVoxelLandWorldConfig* CreateConfig(const bool bDensity, const bool bInfinite)
	{
		UVoxelLandWorldConfig* NewConfig = NewObject<UVoxelLandWorldConfig>();
		NewConfig->TerrainSource =
			bDensity ? EVoxelLandTerrainSource::DirectDensity : EVoxelLandTerrainSource::HeightTerrain;
		NewConfig->WorldExtent = bInfinite ? EVoxelLandWorldExtent::Infinite : EVoxelLandWorldExtent::Finite;
		NewConfig->ChaosCollision.bEnabled = true;
		NewConfig->ChaosCollision.FixedDetail = EVoxelLandChaosCollisionDetail::LOD0;
		NewConfig->bGlobalErosion = false;
		NewConfig->BaseElevationCm = SurfaceCm;
		// Finite initialization verifies the full startup bubble plus field padding.
		NewConfig->HorizontalRadius = 1;
		NewConfig->VerticalRadius = 1;
		NewConfig->OriginXCm = -25600;
		NewConfig->OriginYCm = -25600;
		NewConfig->CellsX = 16;
		NewConfig->CellsY = 16;
		NewConfig->GridSpacingCm = 3200;
		NewConfig->DensityBounds.MinChunk = FIntVector(-2);
		NewConfig->DensityBounds.MaxChunkExclusive = FIntVector(2);
		UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(NewConfig);
		if (bDensity)
		{
			AddNode(*Graph, EVoxelLandBuiltinNode::PositionZ, 1, true);
			AddNode(*Graph, EVoxelLandBuiltinNode::Subtract, 3, true).Pins[1].FloatDefault = SurfaceCm;
			Link(*Graph, 0, 1);
		}
		else
		{
			AddNode(*Graph, EVoxelLandBuiltinNode::FloatConstant, 2, true).Pins[0].FloatDefault = 0.f;
		}
		FVoxelLandGraphNodeRecord& Output = AddNode(*Graph, EVoxelLandBuiltinNode::FloatOutput, 1, false);
		FVoxelLandGraphOutputs::SetName(Output, TEXT("Plane"));
		Link(*Graph, Graph->Nodes.Num() - 2, Graph->Nodes.Num() - 1);
		NewConfig->Graph = Graph;
		NewConfig->TerrainOutputId = Output.Id;
		return NewConfig;
	}

	bool Update() override
	{
		constexpr float StepSeconds = 1.f / 60.f;
		constexpr double TimeoutSeconds = 20.0;
		World.Tick(LEVELTICK_All, StepSeconds);
		if (!Terrain.LastInitializationError.IsEmpty())
		{
			Test.AddError(Terrain.LastInitializationError);
			return Finish();
		}
		if (FPlatformTime::Seconds() - Started > TimeoutSeconds)
		{
			const FVoxelLandChaosStats Stats = Terrain.GetChaosCollisionStats();
			Test.AddError(FString::Printf(
				TEXT(
					"Chaos mode timed out: phase %d, readiness %d, last submitted LOD %d, active bodies %d, failures %lld"),
				static_cast<int32>(Phase), static_cast<int32>(Terrain.GetChaosCollisionReadiness(Consumer)),
				LastCookedLod, Stats.ActiveBodies, static_cast<long long>(Stats.Failures)));
			return Finish();
		}
		if (!Terrain.HasWorldStateForTests())
		{
			return false;
		}
		if (Phase == EPhase::Initialize)
		{
			return BeginLease();
		}
		if (Terrain.GetChaosCollisionReadiness(Consumer) != EVoxelLandChaosReadiness::Ready)
		{
			return false;
		}
		FHitResult Hit;
		const FVector Top(1600, 1600, SurfaceCm + 300.0);
		const FVector Bottom(1600, 1600, SurfaceCm - 300.0);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(VoxelLandChaosWorldModes), true);
		UVoxelLandChaosTerrainComponent* Body = World.LineTraceSingleByChannel(Hit, Top, Bottom, ECC_Pawn, Query)
												  ? Cast<UVoxelLandChaosTerrainComponent>(Hit.GetComponent())
												  : nullptr;
		if (!Body || (Phase == EPhase::Coarse && Body == OriginalBody.Get()))
		{
			return false;
		}
		if (!Test.TestTrue(TEXT("A real cooked terrain shape supplies the expected surface"),
				Body->IsPhysicsStateCreated() && FMath::IsNearlyEqual(Hit.ImpactPoint.Z, SurfaceCm, 2.0)))
		{
			return Finish();
		}
		if (Phase == EPhase::Fine)
		{
			return RequestCoarse(*Body);
		}
		Test.TestEqual(TEXT("The installed replacement was cooked from effective LOD 1"), LastCookedLod, 1);
		Test.TestTrue(TEXT("Effective LOD 1 has fewer triangles than LOD 0"),
			LastCookedTriangles > 0 && LastCookedTriangles < OriginalTriangles);
		Test.TestEqual(TEXT("Data-only mode publishes no visual clipmap revision"),
			Terrain.GetM7PublishedTicketForTests(), uint64(0));
		for (TActorIterator<AVoxelLandRegion> Region(&World); Region; ++Region)
		{
			Test.TestTrue(TEXT("Chaos data-only world has no terrain render regions"), Region->GetOwner() != &Terrain);
		}
		return Finish();
	}

private:
	enum class EPhase : uint8
	{
		Initialize,
		Fine,
		Coarse
	};

	static FVoxelLandGraphNodeRecord& AddNode(
		UVoxelLandTerrainGraph& Graph, const EVoxelLandBuiltinNode Kind, const int32 PinCount, const bool bHasOutput)
	{
		FVoxelLandGraphNodeRecord& Node = Graph.Nodes.AddDefaulted_GetRef();
		const int32 Index = Graph.Nodes.Num();
		Node.Id = FGuid(1, 0, 0, Index);
		Node.OperatorId = VoxelLandBuiltinNodeId(Kind);
		Node.Pins.SetNum(PinCount);
		for (int32 Pin = 0; Pin < PinCount; ++Pin)
		{
			Node.Pins[Pin].Id = FGuid(2, 0, Index, Pin + 1);
		}
		Node.Pins.Last().bOutput = bHasOutput;
		return Node;
	}

	static void Link(UVoxelLandTerrainGraph& Graph, const int32 Source, const int32 Target)
	{
		FVoxelLandGraphLinkRecord& Wire = Graph.Links.AddDefaulted_GetRef();
		Wire.SourceNode = Graph.Nodes[Source].Id;
		Wire.SourcePin = Graph.Nodes[Source].Pins.Last().Id;
		Wire.TargetNode = Graph.Nodes[Target].Id;
		Wire.TargetPin = Graph.Nodes[Target].Pins[0].Id;
	}

	bool Cook(
		TSharedRef<const FVoxelLandChaosMesh, ESPMode::ThreadSafe> Mesh, FVoxelLandChaosCook::FCompletion Completion)
	{
		LastCookedLod = Mesh->EffectiveLod;
		LastCookedTriangles = Mesh->Indices.Num() / 3;
		return FVoxelLandChaosCook::Start(Mesh, MoveTemp(Completion));
	}

	bool BeginLease()
	{
		const FVoxelLandChunkKey Keys[] = {{}};
		const EVoxelLandChaosReadiness Ready = Terrain.UpdateChaosCollisionTiles(Consumer, Keys);
		if (!Test.TestTrue(TEXT("World Config opt-in admits a Chaos lease"),
				Ready == EVoxelLandChaosReadiness::Pending || Ready == EVoxelLandChaosReadiness::Ready))
		{
			return Finish();
		}
		Terrain.SetChaosCookHookForTests(
			[this](TSharedRef<const FVoxelLandChaosMesh, ESPMode::ThreadSafe> Mesh,
				FVoxelLandChaosCook::FCompletion Completion) { return Cook(Mesh, MoveTemp(Completion)); });
		bHasCookHook = true;
		Phase = EPhase::Fine;
		return false;
	}

	bool RequestCoarse(UVoxelLandChaosTerrainComponent& Body)
	{
		Test.TestEqual(TEXT("The initial active shape was cooked from effective LOD 0"), LastCookedLod, 0);
		OriginalBody = &Body;
		OriginalTriangles = LastCookedTriangles;
		const FVoxelLandChunkKey Keys[] = {{}};
		FVoxelLandChaosCollisionOverride Override;
		Override.Mode = EVoxelLandChaosCollisionOverrideMode::Enabled;
		Override.bOverrideDetail = true;
		Override.Detail = EVoxelLandChaosCollisionDetail::LOD1;
		if (!Test.TestTrue(TEXT("Request an LOD 1 override for the leased logical tile"),
				Terrain.SetChaosCollisionOverride(Keys, Override)))
		{
			return Finish();
		}
		Phase = EPhase::Coarse;
		return false;
	}

	bool Finish()
	{
		if (bHasCookHook)
		{
			Terrain.SetChaosCookHookForTests({});
		}
		Terrain.ReleaseChaosCollisionLease(Consumer);
		Terrain.Shutdown();
		World.EndPlay(EEndPlayReason::Destroyed);
		World.DestroyWorld(false);
		GEngine->DestroyWorldContext(&World);
		World.RemoveFromRoot();
		return true;
	}

	FAutomationTestBase& Test;
	UWorld& World;
	AVoxelLandWorld& Terrain;
	TStrongObjectPtr<UVoxelLandWorldConfig> Config;
	TWeakObjectPtr<UVoxelLandChaosTerrainComponent> OriginalBody;
	const FGuid Consumer = FGuid::NewGuid();
	const double Started = FPlatformTime::Seconds();
	int32 LastCookedLod = INDEX_NONE;
	int32 LastCookedTriangles = 0;
	int32 OriginalTriangles = 0;
	bool bHasCookHook = false;
	EPhase Phase = EPhase::Initialize;
};

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FVoxelLandWorldConfigChaosTest, "VoxelLand.Collision.Chaos.WorldConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FVoxelLandWorldConfigChaosTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	Names = {TEXT("FiniteHeight"), TEXT("InfiniteHeight"), TEXT("FiniteDensity"), TEXT("InfiniteDensity")};
	Commands = Names;
}

bool FVoxelLandWorldConfigChaosTest::RunTest(const FString& Parameters)
{
	using FCommand = FVoxelLandWorldConfigChaosCommand;
	if (!TestNotNull(TEXT("UE engine available"), GEngine))
	{
		return false;
	}
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
												  .CreatePhysicsScene(true)
												  .ShouldSimulatePhysics(true)
												  .EnableTraceCollision(true)
												  .CreateNavigation(false)
												  .CreateAISystem(false);
	UWorld* World =
		UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Created a headless game world with physics"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	AVoxelLandWorld* Terrain = World->SpawnActor<AVoxelLandWorld>();
	if (!TestNotNull(TEXT("Spawned data-only terrain"), Terrain))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}
	Terrain->bDataOnly = true;
	Terrain->WorkerCount = 1;
	World->GetWorldSettings()->NotifyBeginPlay();
	UVoxelLandWorldConfig* Config =
		FCommand::CreateConfig(Parameters.EndsWith(TEXT("Density")), Parameters.StartsWith(TEXT("Infinite")));
	if (!TestTrue(
			TEXT("Begin World Config initialization"), Terrain->InitializeFromConfig(Config, FIntVector::ZeroValue)))
	{
		AddError(Terrain->LastInitializationError);
		Terrain->Shutdown();
		World->EndPlay(EEndPlayReason::Destroyed);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FCommand>(*this, *World, *Terrain, *Config));
	return true;
}

class FVoxelLandChaosRenderMappingCommand final : public IAutomationLatentCommand
{
public:
	FVoxelLandChaosRenderMappingCommand(
		FAutomationTestBase& InTest, UWorld& InWorld, AVoxelLandWorld& InTerrain, UVoxelLandWorldConfig& InConfig)
		: Test(InTest)
		, World(InWorld)
		, Terrain(InTerrain)
		, Config(&InConfig)
	{
	}

	bool Update() override
	{
		constexpr double TimeoutSeconds = 35.0;
		constexpr float StepSeconds = 1.f / 60.f;
		World.Tick(LEVELTICK_All, StepSeconds);
		if (!Terrain.LastInitializationError.IsEmpty())
		{
			Test.AddError(Terrain.LastInitializationError);
			return Finish();
		}

		const int32 RenderLod = FindPublishedRenderLod();
		FVoxelLandChaosTileInfo Info;
		const bool bHasTile = Terrain.GetChaosCollisionTileInfo(Tile, Info);
		if (FPlatformTime::Seconds() - Started > TimeoutSeconds)
		{
			Test.AddError(FString::Printf(
				TEXT(
					"Render mapping stalled at step %d: published LOD %d, requested %d, installed %d, has shape %d, readiness %d"),
				Step, RenderLod, Info.RequestedLod, Info.InstalledLod, Info.bHasShape,
				static_cast<int32>(Terrain.GetChaosCollisionReadiness(Consumer))));
			return Finish();
		}
		if (!Terrain.HasWorldStateForTests())
		{
			return false;
		}
		if (!bViewRequested)
		{
			RequestView();
			return false;
		}
		const uint64 Ticket = Terrain.GetM7PublishedTicketForTests();
		if (Ticket == PreviousTicket || RenderLod != ExpectedRenderLods[Step])
		{
			return false;
		}
		if (!bHasLease)
		{
			const FVoxelLandChunkKey Tiles[] = {Tile};
			const EVoxelLandChaosReadiness Readiness = Terrain.UpdateChaosCollisionTiles(Consumer, Tiles);
			if (!Test.TestTrue(TEXT("Admit a bounded lease over the stable base tile"),
					Readiness == EVoxelLandChaosReadiness::Pending || Readiness == EVoxelLandChaosReadiness::Ready))
			{
				return Finish();
			}
			bHasLease = true;
			return false;
		}
		if (!bHasTile || !CheckCollisionForRenderLod(RenderLod, Info))
		{
			return false;
		}

		Test.AddInfo(
			FString::Printf(TEXT("Published render LOD %d mapped to collision LOD %d at the same logical tile"),
				RenderLod, Info.InstalledLod));
		++Step;
		if (Step == UE_ARRAY_COUNT(ExpectedRenderLods))
		{
			return Finish();
		}
		PreviousTicket = Ticket;
		RequestView();
		return false;
	}

private:
	static constexpr double SurfaceCm = FVoxelLandWorldConfigChaosCommand::SurfaceCm;
	static constexpr double ProbeXYCm = 1600.0;
	static constexpr int32 ExpectedRenderLods[] = {0, 1, 2, 0};
	static constexpr double ViewLocationsX[] = {1600.0, -13000.0, -24000.0, 1600.0};

	void RequestView()
	{
		Terrain.SetTestViewLocation(FVector(ViewLocationsX[Step], ProbeXYCm, SurfaceCm + 300.0));
		bViewRequested = true;
	}

	int32 FindPublishedRenderLod() const
	{
		TArray<FVoxelLandChunkKey> Leaves;
		Terrain.CopyM7PublishedKeysForTests(Leaves);
		// The probe lies within the XY footprint of the origin tile at every planar LOD.
		for (const FVoxelLandChunkKey& Key : Leaves)
		{
			if (Key.Coordinate == FIntVector::ZeroValue)
			{
				return Key.LOD;
			}
		}
		return INDEX_NONE;
	}

	bool CheckCollisionForRenderLod(const int32 RenderLod, const FVoxelLandChaosTileInfo& Info)
	{
		FHitResult Hit;
		const FVector Top(ProbeXYCm, ProbeXYCm, SurfaceCm + 300.0);
		const FVector Bottom(ProbeXYCm, ProbeXYCm, SurfaceCm - 300.0);
		const FCollisionQueryParams Query(SCENE_QUERY_STAT(VoxelLandChaosRenderMapping), true);
		const bool bTraceHits = World.LineTraceSingleByChannel(Hit, Top, Bottom, ECC_Pawn, Query);
		const EVoxelLandChaosReadiness Readiness = Terrain.GetChaosCollisionReadiness(Consumer);
		if (RenderLod == 0)
		{
			if (!Info.bCoverageReady || !Info.bHasShape || Info.RequestedLod != 1 || Info.InstalledLod != 1
				|| Readiness != EVoxelLandChaosReadiness::Ready || !bTraceHits)
			{
				return false;
			}
			Test.TestTrue(TEXT("The effective LOD 1 body blocks Pawn at the same terrain height"),
				Cast<UVoxelLandChaosTerrainComponent>(Hit.GetComponent())
					&& FMath::IsNearlyEqual(Hit.ImpactPoint.Z, SurfaceCm, 2.0));
			return true;
		}
		return !Info.bCoverageReady && !Info.bHasShape && Info.RequestedLod == INDEX_NONE
			&& Info.InstalledLod == INDEX_NONE && Readiness == EVoxelLandChaosReadiness::Disabled && !bTraceHits;
	}

	bool Finish()
	{
		Terrain.ReleaseChaosCollisionLease(Consumer);
		Terrain.Shutdown();
		World.EndPlay(EEndPlayReason::Destroyed);
		World.DestroyWorld(false);
		GEngine->DestroyWorldContext(&World);
		World.RemoveFromRoot();
		return true;
	}

	FAutomationTestBase& Test;
	UWorld& World;
	AVoxelLandWorld& Terrain;
	TStrongObjectPtr<UVoxelLandWorldConfig> Config;
	const FVoxelLandChunkKey Tile{};
	const FGuid Consumer = FGuid::NewGuid();
	const double Started = FPlatformTime::Seconds();
	uint64 PreviousTicket = 0;
	int32 Step = 0;
	bool bViewRequested = false;
	bool bHasLease = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandChaosRenderMappingTest,
	"VoxelLand.Collision.Chaos.WorldConfig.RenderLodMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandChaosRenderMappingTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("UE engine available"), GEngine))
	{
		return false;
	}
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
												  .CreatePhysicsScene(true)
												  .ShouldSimulatePhysics(true)
												  .EnableTraceCollision(true)
												  .CreateNavigation(false)
												  .CreateAISystem(false);
	UWorld* World =
		UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Created a game world with visual clipmap and physics"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	AVoxelLandWorld* Terrain = World->SpawnActor<AVoxelLandWorld>();
	if (!TestNotNull(TEXT("Spawned rendered terrain"), Terrain))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}
	Terrain->bEnableM7Clipmap = true;
	Terrain->M7MaxLOD = 2;
	Terrain->M7HorizontalRadiusCm = 35000.0;
	Terrain->M7MaxLeaves = 512;
	World->GetWorldSettings()->NotifyBeginPlay();
	UVoxelLandWorldConfig* Config = FVoxelLandWorldConfigChaosCommand::CreateConfig(false, true);
	Config->ChaosCollision.bMapByRenderLod = true;
	Config->ChaosCollision.UnresolvedRenderDetail = EVoxelLandChaosCollisionDetail::Disabled;
	Config->ChaosCollision.RenderLodDetails[0] = EVoxelLandChaosCollisionDetail::LOD1;
	Config->ChaosCollision.RenderLodDetails[1] = EVoxelLandChaosCollisionDetail::Disabled;
	Config->ChaosCollision.RenderLodDetails[2] = EVoxelLandChaosCollisionDetail::Disabled;
	if (!TestTrue(TEXT("Initialize the mapped infinite Height world"),
			Terrain->InitializeFromConfig(Config, FIntVector::ZeroValue)))
	{
		AddError(Terrain->LastInitializationError);
		Terrain->Shutdown();
		World->EndPlay(EEndPlayReason::Destroyed);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}
	FAutomationTestFramework::Get().EnqueueLatentCommand(
		MakeShared<FVoxelLandChaosRenderMappingCommand>(*this, *World, *Terrain, *Config));
	return true;
}

#endif
