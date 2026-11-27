#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Graph/Sources/VoxelLandGraphOutputs.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "VoxelLandPCGCell.h"
#include "VoxelLandPCGComponent.h"
#include "VoxelLandPCGGetVoxelTerrainSettings.h"
#include "VoxelLandPCGVoxelProjectSettings.h"
#include "VoxelLandPCGVoxelSurfaceSamplerSettings.h"
#include "VoxelLandPCGWorld.h"

#include "Camera/CameraActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Elements/PCGTransformPoints.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "PCGEdge.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "UObject/StrongObjectPtr.h"

class FVoxelLandPCGDirectDensityCommand final : public IAutomationLatentCommand
{
public:
	FVoxelLandPCGDirectDensityCommand(FAutomationTestBase& InTest, bool bInInfinite)
		: Test(InTest)
		, bInfinite(bInInfinite)
	{
	}

	~FVoxelLandPCGDirectDensityCommand() override { CloseWorld(); }

	bool Update() override
	{
		if (!World)
		{
			return !OpenWorld();
		}
		World->Tick(LEVELTICK_All, 1.0f / 60.0f);
		if (FPlatformTime::Seconds() - Started > TimeoutSeconds)
		{
			Test.AddError(FString::Printf(TEXT("Direct Density PCG did not publish points. Terrain: %s. PCG: %s."),
				*Runtime->LastInitializationError, *Runtime->Placement->PlacementStatus));
			return true;
		}
		if (Runtime->Placement->GetVisibleInstanceCount() == 0)
		{
			return false;
		}
		CheckPublishedPoints();
		Runtime->Placement->ClearPreview();
		Test.TestEqual(TEXT("Clear retires the Direct Density cosmetic instances"),
			Runtime->Placement->GetVisibleInstanceCount(), 0);
		return true;
	}

private:
	static constexpr double FloorCm = 700.0;
	static constexpr double ProbeOffsetCm = 200.0;
	static constexpr double ProjectionLimitCm = 400.0;
	static constexpr double RuntimeCellSizeCm = 3200.0;
	static constexpr double TimeoutSeconds = 35.0;

	static int32 AddTerrainNode(UVoxelLandTerrainGraph& Graph, EVoxelLandBuiltinNode Kind, int32 PinCount)
	{
		const int32 NodeIndex = Graph.Nodes.Num();
		FVoxelLandGraphNodeRecord& Node = Graph.Nodes.AddDefaulted_GetRef();
		Node.Id = FGuid(0x50434744, 0, 0, NodeIndex + 1);
		Node.OperatorId = VoxelLandBuiltinNodeId(Kind);
		Node.Pins.SetNum(PinCount);
		for (int32 PinIndex = 0; PinIndex < PinCount; ++PinIndex)
		{
			Node.Pins[PinIndex].Id = FGuid(0x50434744, 0, NodeIndex + 1, PinIndex + 1);
		}
		Node.Pins.Last().bOutput = Kind != EVoxelLandBuiltinNode::FloatOutput;
		return NodeIndex;
	}

	static void LinkTerrain(UVoxelLandTerrainGraph& Graph, int32 From, int32 To)
	{
		FVoxelLandGraphLinkRecord& Link = Graph.Links.AddDefaulted_GetRef();
		Link.SourceNode = Graph.Nodes[From].Id;
		Link.SourcePin = Graph.Nodes[From].Pins.Last().Id;
		Link.TargetNode = Graph.Nodes[To].Id;
		Link.TargetPin = Graph.Nodes[To].Pins[0].Id;
	}

	void MakeTerrainConfig()
	{
		Config.Reset(NewObject<UVoxelLandWorldConfig>(GetTransientPackage()));
		TerrainGraph.Reset(NewObject<UVoxelLandTerrainGraph>(Config.Get()));
		UVoxelLandTerrainGraph* Terrain = TerrainGraph.Get();
		const int32 Z = AddTerrainNode(*Terrain, EVoxelLandBuiltinNode::PositionZ, 1);
		const int32 Subtract = AddTerrainNode(*Terrain, EVoxelLandBuiltinNode::Subtract, 3);
		const int32 Output = AddTerrainNode(*Terrain, EVoxelLandBuiltinNode::FloatOutput, 1);
		Terrain->Nodes[Subtract].Pins[1].FloatDefault = static_cast<float>(FloorCm);
		FVoxelLandGraphOutputs::SetName(Terrain->Nodes[Output], TEXT("Flat Density"));
		LinkTerrain(*Terrain, Z, Subtract);
		LinkTerrain(*Terrain, Subtract, Output);

		Config->Graph = Terrain;
		Config->TerrainOutputId = Terrain->Nodes[Output].Id;
		Config->TerrainSource = EVoxelLandTerrainSource::DirectDensity;
		Config->WorldExtent = bInfinite ? EVoxelLandWorldExtent::Infinite : EVoxelLandWorldExtent::Finite;
		Config->DirectVoxelSizeCm = 100.0f;
		Config->DensityBounds.MinChunk = FIntVector(0, 0, -1);
		Config->DensityBounds.MaxChunkExclusive = FIntVector(1, 1, 1);
		Config->ChaosCollision.bEnabled = false;
	}

	static bool Connect(UPCGGraph& InGraph, UPCGNode* From, FName Output, UPCGNode* To, FName Input)
	{
		const UPCGPin* Source = From->GetOutputPin(Output);
		const UPCGPin* Target = To->GetInputPin(Input);
		if (!Source || !Target)
		{
			return false;
		}
		InGraph.AddEdge(From, Output, To, Input);
		for (const UPCGEdge* Edge : Target->Edges)
		{
			if (Edge && Edge->GetOtherPin(Target) == Source)
			{
				return true;
			}
		}
		return false;
	}

	bool MakePCGGraph()
	{
		Graph.Reset(NewObject<UPCGGraph>(GetTransientPackage(), NAME_None, RF_Transient));
		UVoxelLandPCGGetVoxelTerrainSettings* GetSettings = nullptr;
		UVoxelLandPCGVoxelSurfaceSamplerSettings* SamplerSettings = nullptr;
		UPCGTransformPointsSettings* LiftSettings = nullptr;
		UVoxelLandPCGVoxelProjectSettings* ProjectSettings = nullptr;
		UPCGNode* Get = Graph->AddNodeOfType(GetSettings);
		UPCGNode* Sample = Graph->AddNodeOfType(SamplerSettings);
		UPCGNode* Lift = Graph->AddNodeOfType(LiftSettings);
		UPCGNode* Project = Graph->AddNodeOfType(ProjectSettings);
		UPCGNode* Output = Graph->GetOutputNode();
		if (!Get || !Sample || !Lift || !Project || !Output)
		{
			return false;
		}
		SamplerSettings->MinimumSpacingCm = 500.0;
		LiftSettings->OffsetMin = FVector(0, 0, ProbeOffsetCm);
		LiftSettings->OffsetMax = LiftSettings->OffsetMin;
		ProjectSettings->MaximumDistanceCm = ProjectionLimitCm;
		ProjectSettings->Direction = FVector::DownVector;
		const FName TerrainPin(TEXT("Terrain"));
		return Connect(*Graph, Get, TerrainPin, Sample, TerrainPin)
			&& Connect(*Graph, Get, TerrainPin, Project, TerrainPin)
			&& Connect(*Graph, Sample, PCGPinConstants::DefaultOutputLabel, Lift, PCGPinConstants::DefaultInputLabel)
			&& Connect(*Graph, Lift, PCGPinConstants::DefaultOutputLabel, Project, PCGPinConstants::DefaultInputLabel)
			&& Connect(
				*Graph, Project, PCGPinConstants::DefaultOutputLabel, Output, PCGPinConstants::DefaultOutputLabel);
	}

	bool OpenWorld()
	{
		MakeTerrainConfig();
		if (!Test.TestTrue(TEXT("Build the native projection graph"), MakePCGGraph()))
		{
			return false;
		}
		const UWorld::InitializationValues Init = UWorld::InitializationValues()
													  .CreatePhysicsScene(false)
													  .ShouldSimulatePhysics(false)
													  .CreateNavigation(false)
													  .CreateAISystem(false);
		World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
		if (!Test.TestNotNull(TEXT("Create isolated Direct Density runtime world"), World))
		{
			return false;
		}
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		Runtime = World->SpawnActor<AVoxelLandPCGWorld>();
		APlayerController* Player = World->SpawnActor<APlayerController>();
		ACameraActor* Camera = World->SpawnActor<ACameraActor>(FVector(1600, 1600, 1200), FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("Spawn Direct Density world"), Runtime)
			|| !Test.TestNotNull(TEXT("Spawn local source"), Player)
			|| !Test.TestNotNull(TEXT("Spawn placement camera"), Camera))
		{
			return false;
		}
		Runtime->Config = Config.Get();
		Runtime->WorkerCount = 2;
		FVoxelLandPCGLayer& Layer = Runtime->Placement->Layers.AddDefaulted_GetRef();
		Layer.Graph = Graph.Get();
		Layer.StaticMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
		if (!Test.TestNotNull(TEXT("Load transient layer marker"), Layer.StaticMesh.Get()))
		{
			return false;
		}
		Layer.GenerationRadiusCm = 3800.0;
		Layer.RetirementMarginCm = 0.0;
		Runtime->Placement->MaxResidentCells = 24;
		Player->SetAsLocalPlayerController();
		World->GetWorldSettings()->NotifyBeginPlay();
		Player->SetViewTarget(Camera);
		Started = FPlatformTime::Seconds();
		return true;
	}

	void CheckPublishedPoints()
	{
		int32 Instances = 0;
		TSet<FIntVector> Roots;
		for (TActorIterator<AVoxelLandPCGCell> It(World); It; ++It)
		{
			const TInlineComponentArray<UInstancedStaticMeshComponent*> Components(*It);
			for (const UInstancedStaticMeshComponent* Component : Components)
			{
				Test.TestTrue(TEXT("The generated cell and its cosmetic component are transient"),
					It->HasAnyFlags(RF_Transient) && Component->HasAnyFlags(RF_Transient));
				Test.TestFalse(TEXT("Cosmetic cells do not replicate"), It->GetIsReplicated());
				Test.TestEqual(TEXT("Cosmetic instances do not add collision"), Component->GetCollisionEnabled(),
					ECollisionEnabled::NoCollision);
				Test.TestFalse(
					TEXT("Cosmetic instances do not add overlap events"), Component->GetGenerateOverlapEvents());
				for (int32 Index = 0; Index < Component->GetInstanceCount(); ++Index)
				{
					FTransform Transform;
					if (Test.TestTrue(TEXT("Read projected instance transform"),
							Component->GetInstanceTransform(Index, Transform, true)))
					{
						const FVector Position = Transform.GetLocation();
						const FIntVector Root(FMath::RoundToInt(Position.X), FMath::RoundToInt(Position.Y),
							FMath::RoundToInt(Position.Z));
						Test.TestFalse(TEXT("Each projected marker has one cell owner"), Roots.Contains(Root));
						Roots.Add(Root);
						Test.TestTrue(
							TEXT("Native projection returns the lifted candidate to the Direct Density surface"),
							FMath::IsNearlyEqual(Position.Z, FloorCm, 0.2));
						if (!bInfinite)
						{
							Test.TestTrue(TEXT("Finite Direct Density clips cosmetic placement"),
								Position.X >= 0 && Position.X < RuntimeCellSizeCm && Position.Y >= 0
									&& Position.Y < RuntimeCellSizeCm);
						}
						++Instances;
					}
				}
			}
		}
		Test.TestTrue(TEXT("Direct Density produces at least one projected cosmetic instance"), Instances > 0);
	}

	void CloseWorld()
	{
		if (!World)
		{
			return;
		}
		if (Runtime)
		{
			Runtime->Placement->ClearPreview();
		}
		if (World->HasBegunPlay())
		{
			World->EndPlay(EEndPlayReason::Destroyed);
		}
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		World = nullptr;
	}

	FAutomationTestBase& Test;
	const bool bInfinite;
	UWorld* World = nullptr;
	AVoxelLandPCGWorld* Runtime = nullptr;
	TStrongObjectPtr<UVoxelLandWorldConfig> Config;
	TStrongObjectPtr<UVoxelLandTerrainGraph> TerrainGraph;
	TStrongObjectPtr<UPCGGraph> Graph;
	double Started = 0.0;
};

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FVoxelLandPCGDirectDensity, "VoxelLand.PCG.DirectDensity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FVoxelLandPCGDirectDensity::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	Names = {TEXT("FiniteRuntime"), TEXT("InfiniteRuntime")};
	Commands = Names;
}

bool FVoxelLandPCGDirectDensity::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("Run without NullRHI to verify runtime Direct Density cosmetic publication."));
		return true;
	}
	if (!TestNotNull(TEXT("Editor engine"), GEngine))
	{
		return false;
	}
	FAutomationTestFramework::Get().EnqueueLatentCommand(
		MakeShared<FVoxelLandPCGDirectDensityCommand>(*this, Parameters == TEXT("InfiniteRuntime")));
	return true;
}

#endif
