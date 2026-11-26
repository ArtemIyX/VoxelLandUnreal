#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Graph/Sources/VoxelLandGraphOutputs.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Generation/Collision/VoxelLandSdfCollision.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "World/Collision/VoxelLandCollisionWorldView.h"
#include "World/Demo/VoxelLandClipmapDemo.h"
#include "World/VoxelLandWorld.h"

class FVoxelLandConfigPlayCommand final : public IAutomationLatentCommand
{
public:
	static constexpr int32 DirectVoxelSizeCm = 100;
	static constexpr int32 RemoteChunkX = 12;
	static constexpr float RemoteCm = RemoteChunkX * FVoxelLandChunkData::Cells * DirectVoxelSizeCm;

	static void MakeIsland(UVoxelLandWorldConfig& Config)
	{
		UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(&Config);
		const EVoxelLandBuiltinNode Kinds[] = {EVoxelLandBuiltinNode::PositionX, EVoxelLandBuiltinNode::PositionY,
			EVoxelLandBuiltinNode::PositionZ, EVoxelLandBuiltinNode::Sphere, EVoxelLandBuiltinNode::FloatOutput,
			EVoxelLandBuiltinNode::FloatParameter, EVoxelLandBuiltinNode::Subtract, EVoxelLandBuiltinNode::Sphere,
			EVoxelLandBuiltinNode::Union};
		const int32 PinCounts[] = {1, 1, 1, 5, 1, 1, 3, 5, 3};
		for (int32 NodeIndex = 0; NodeIndex < UE_ARRAY_COUNT(Kinds); ++NodeIndex)
		{
			FVoxelLandGraphNodeRecord& Node = Graph->Nodes.AddDefaulted_GetRef();
			Node.Id = FGuid(1, 0, 0, NodeIndex + 1);
			Node.OperatorId = VoxelLandBuiltinNodeId(Kinds[NodeIndex]);
			Node.Pins.SetNum(PinCounts[NodeIndex]);
			for (int32 PinIndex = 0; PinIndex < Node.Pins.Num(); ++PinIndex)
			{
				Node.Pins[PinIndex].Id = FGuid(2, 0, NodeIndex + 1, PinIndex + 1);
			}
			Node.Pins.Last().bOutput = Kinds[NodeIndex] != EVoxelLandBuiltinNode::FloatOutput;
		}
		FVoxelLandGraphParameter& Radius = Graph->Parameters.AddDefaulted_GetRef();
		Radius.Id = FGuid(3, 0, 0, 1);
		Radius.Name = TEXT("Radius");
		Radius.FloatDefault = 100.f;
		Graph->Nodes[5].ParameterId = Radius.Id;
		FVoxelLandParameterOverride& Override = Config.Overrides.AddDefaulted_GetRef();
		Override.ParameterId = Radius.Id;
		Override.FloatValue = 400.f;
		const auto Link = [Graph](int32 Source, int32 Target, int32 Input) {
			FVoxelLandGraphLinkRecord& Wire = Graph->Links.AddDefaulted_GetRef();
			Wire.SourceNode = Graph->Nodes[Source].Id;
			Wire.SourcePin = Graph->Nodes[Source].Pins.Last().Id;
			Wire.TargetNode = Graph->Nodes[Target].Id;
			Wire.TargetPin = Graph->Nodes[Target].Pins[Input].Id;
		};
		Graph->Nodes[6].Pins[1].FloatDefault = RemoteCm;
		Link(0, 3, 0);
		Link(1, 3, 1);
		Link(2, 3, 2);
		Link(5, 3, 3);
		Link(0, 6, 0);
		Link(6, 7, 0);
		Link(1, 7, 1);
		Link(2, 7, 2);
		Link(5, 7, 3);
		Link(3, 8, 0);
		Link(7, 8, 1);
		Link(8, 4, 0);
		Config.DirectVoxelSizeCm = DirectVoxelSizeCm;
		FVoxelLandGraphOutputs::SetName(Graph->Nodes[4], TEXT("Island Density"));
		Config.Graph = Graph;
		Config.TerrainOutputId = Graph->Nodes[4].Id;
	}

	FVoxelLandConfigPlayCommand(FAutomationTestBase& InTest, UVoxelLandWorldConfig& InConfig, bool bInUseDemo)
		: Test(InTest)
		, Config(&InConfig)
		, bUseDemo(bInUseDemo)
	{
	}

	bool Update() override
	{
		if (bEnding)
		{
			return !GEditor->PlayWorld;
		}
		if (FPlatformTime::Seconds() - Start > 30.0)
		{
			Test.AddError(Actor.IsValid() ? Actor->LastInitializationError + Actor->M7ClipmapError
												+ TEXT(" World Config Play timed out.")
										  : TEXT("Play session did not begin."));
			return Finish();
		}
		UWorld* World = GEditor->PlayWorld;
		if (!World || !World->HasBegunPlay())
		{
			return false;
		}
		if (!Actor.IsValid())
		{
			return StartTerrain(*World);
		}
		AVoxelLandWorld& Terrain = *Actor.Get();
		if (!Terrain.LastInitializationError.IsEmpty())
		{
			Test.AddError(Terrain.LastInitializationError);
			return Finish();
		}
		if (!Terrain.HasWorldStateForTests())
		{
			return false;
		}
		return Config->IsDirectDensity() ? CheckDirect(Terrain) : CheckHeight(*World, Terrain);
	}

private:
	static bool IsHeadless() { return !FApp::CanEverRender() || FParse::Param(FCommandLine::Get(), TEXT("nullrhi")); }

	TOptional<double> FindWorldHeight(const AVoxelLandWorld& Terrain) const
	{
		constexpr int32 Samples = FVoxelLandChunkData::SamplesPerAxis;
		constexpr int32 Cells = FVoxelLandChunkData::Cells;
		constexpr int32 Mid = Cells / 2 + 1;
		constexpr int32 XY = Mid + Samples * Mid;
		for (int32 Delta = -3; Delta <= 3; ++Delta)
		{
			FVoxelLandChunkKey Key;
			Key.Coordinate.Z = BaseChunkZ + Delta;
			const auto Chunk = Terrain.FindChunk(Key);
			if (!Chunk || Chunk->Density.Num() != FVoxelLandChunkData::SampleCount)
			{
				continue;
			}
			for (int32 Z = 0; Z < Cells; ++Z)
			{
				const int32 Index = XY + Samples * Samples * (Z + 1);
				const int32 Lower = Chunk->Density[Index], Upper = Chunk->Density[Index + Samples * Samples];
				if (Lower < 0 && Upper >= 0)
				{
					const double Fraction = -double(Lower) / double(Upper - Lower);
					return (double(Key.Coordinate.Z) * Cells + Z + Fraction) * Config->VoxelSizeCm;
				}
			}
		}
		return {};
	}

	bool CheckCollisionGround(AVoxelLandWorld& Terrain, const FVector& LocalSurface)
	{
		FVoxelLandCapsuleSweepRequest Sweep;
		const FVector LocalStart = LocalSurface + FVector(0, 0, 500.0);
		const FVector LocalEnd = LocalSurface + FVector(0, 0, 20.0);
		const FVector Padding(Sweep.RadiusCm + Sweep.SkinCm, Sweep.RadiusCm + Sweep.SkinCm,
			Sweep.HalfHeightCm + Sweep.SkinCm);
		FVoxelLandCollisionWorldView Collision;
		if (!Test.TestTrue(TEXT("Admit the capsule grounding footprint"),
				Terrain.UpdateCollisionDemandForBounds(CollisionConsumer, FBox(LocalEnd - Padding, LocalStart + Padding)))
			|| !Test.TestTrue(TEXT("Capture the collision field"), Terrain.CaptureCollisionView(CollisionConsumer, Collision)))
		{
			Finish();
			return false;
		}
		const FVector Origin(Collision.OriginLocation);
		Sweep.Start = LocalStart + Origin;
		Sweep.End = LocalEnd + Origin;
		const FVoxelLandCapsuleSweepResult Hit = FVoxelLandSdfCollision::SweepCapsule(*Collision.Field, Sweep);
		if (Hit.Status == EVoxelLandCapsuleSweepStatus::Unavailable)
		{
			return false;
		}
		const FString Diagnostic = FString::Printf(TEXT("Capsule grounds in Play (status %d, normal %s, safe %.4f, regions %d)"),
			int32(Hit.Status), *Hit.Normal.ToString(), Hit.LastSafeTime, Hit.RegionsVisited);
		Test.TestTrue(*Diagnostic,
			Hit.Status == EVoxelLandCapsuleSweepStatus::Contact && Hit.Normal.Z > 0.3 && Hit.LastSafeTime > 0.0);
		Terrain.ReleaseCollisionDemand(CollisionConsumer);
		return true;
	}

	bool CheckHeight(UWorld& World, AVoxelLandWorld& Terrain)
	{
		constexpr int32 Cells = FVoxelLandChunkData::Cells;
		if (!bHeightProbesRequested)
		{
			BaseChunkZ = FMath::FloorToInt(double(Config->BaseElevationCm) / (Cells * double(Config->VoxelSizeCm)));
			for (int32 Delta = -3; Delta <= 3; ++Delta)
			{
				if (!Test.TestTrue(TEXT("Explicit Height probe admitted"),
						Terrain.RequestDataChunk(FIntVector(0, 0, BaseChunkZ + Delta))))
				{
					return Finish();
				}
			}
			bHeightProbesRequested = true;
		}
		if (!bViewSet)
		{
			const TOptional<double> Height = FindWorldHeight(Terrain);
			if (!Height.IsSet())
			{
				return false;
			}
			const double Mid = 0.5 * Cells * Config->VoxelSizeCm;
			if (!CheckCollisionGround(Terrain, FVector(Mid, Mid, Height.GetValue())))
			{
				return false;
			}
			if (IsHeadless())
			{
				Test.TestEqual(TEXT("Headless Height has no clipmap publication"),
					Terrain.GetM7PublishedTicketForTests(), uint64(0));
				return Finish();
			}
			Terrain.SetTestViewLocation(FVector(Mid, Mid, Height.GetValue()));
			bViewSet = true;
			return false;
		}
		if (Terrain.GetM7PublishedTicketForTests() == 0 || Terrain.IsM7ReplacementPendingForTests())
		{
			return false;
		}
		TArray<FVoxelLandChunkKey> Leaves;
		Terrain.CopyM7PublishedKeysForTests(Leaves);
		for (TActorIterator<AVoxelLandRegion> Region(&World); Region; ++Region)
		{
			if (Region->GetOwner() == &Terrain && Leaves.ContainsByPredicate([&Region](const FVoxelLandChunkKey& Key) {
					return Region->IsChunkEffectivelyVisible(Key.Coordinate);
				}))
			{
				return Finish();
			}
		}
		Test.AddError(TEXT("The clipmap published a layout without a visible terrain mesh at the measured surface."));
		return Finish();
	}

	bool CheckDirect(AVoxelLandWorld& Terrain)
	{
		if (!bViewSet)
		{
			Terrain.SetTestViewLocation(FVector(1600, 1600, 1600));
			bViewSet = true;
			return false;
		}
		FVoxelLandChunkKey Key;
		Key.Coordinate.X = bMovedToRemote ? RemoteChunkX : 0;
		const auto Island = Terrain.FindChunk(Key);
		if (!Island)
		{
			return false;
		}
		constexpr int32 Axis = FVoxelLandChunkData::SamplesPerAxis;
		constexpr int32 Center = 1 + Axis * (1 + Axis);
		Test.TestTrue(TEXT("Island interior is solid"), Island->Density[Center] < 0);
		Test.TestTrue(TEXT("Projected density uses the 400 cm override instead of the 100 cm default"),
			Island->Density[Center + Axis * Axis * 3] < 0);
		Test.TestTrue(TEXT("Air above the island is positive"), Island->Density[Center + Axis * Axis * 6] > 0);
		if (!CheckCollisionGround(Terrain, FVector((bMovedToRemote ? RemoteCm : 0.0) + 150.0, 150.0, 325.0)))
		{
			return false;
		}
		Test.TestFalse(TEXT("Island arrives through camera demand"), Terrain.IsExplicitChunkForTests(Key));
		Test.TestEqual(
			TEXT("Direct Density has no clipmap revision"), Terrain.GetM7PublishedTicketForTests(), uint64(0));
		if (Config->IsInfinite() && !bMovedToRemote)
		{
			bMovedToRemote = true;
			Terrain.SetTestViewLocation(FVector(RemoteCm, 1600, 1600));
			return false;
		}
		return Finish();
	}

	bool StartTerrain(UWorld& World)
	{
		AVoxelLandClipmapDemo* Demo = bUseDemo ? World.SpawnActorDeferred<AVoxelLandClipmapDemo>(
													 AVoxelLandClipmapDemo::StaticClass(), FTransform::Identity)
											   : nullptr;
		Actor = bUseDemo ? Demo : World.SpawnActor<AVoxelLandWorld>();
		if (!Test.TestNotNull(TEXT("Terrain actor spawned"), Actor.Get()))
		{
			return Finish();
		}
		Actor->bDataOnly = !Config->IsDirectDensity() && IsHeadless();
		Actor->bEnableM7Clipmap = true;
		Actor->M7MaxLOD = 1;
		Actor->M7MaxLeaves = 16;
		Actor->M7HorizontalRadiusCm = 600;
		Actor->M7VerticalRadiusCm = 400;
		if (Demo)
		{
			const FObjectProperty* Property = FindFProperty<FObjectProperty>(Demo->GetClass(), TEXT("TerrainConfig"));
			if (!Test.TestNotNull(TEXT("Clipmap Demo has an instance-editable World Config slot"), Property))
			{
				return Finish();
			}
			Property->SetObjectPropertyValue_InContainer(Demo, Config.Get());
			Demo->FinishSpawning(FTransform::Identity);
			return false;
		}
		if (Config->IsDirectDensity())
		{
			Test.TestFalse(TEXT("Clipmap refused for Direct Density"),
				Actor->InitializeFromConfig(Config.Get(), FIntVector::ZeroValue));
			Actor->bEnableM7Clipmap = false;
		}
		const FIntVector Startup = Config->IsDirectDensity() ? FIntVector(1, 0, 0) : FIntVector::ZeroValue;
		if (Actor->InitializeFromConfig(Config.Get(), Startup))
		{
			return false;
		}
		Test.AddError(Actor->LastInitializationError);
		return Finish();
	}

	bool Finish()
	{
		if (Actor.IsValid())
		{
			Actor->Shutdown();
		}
		bEnding = true;
		GEditor->RequestEndPlayMap();
		return false;
	}

	FAutomationTestBase& Test;
	TWeakObjectPtr<AVoxelLandWorld> Actor;
	TStrongObjectPtr<UVoxelLandWorldConfig> Config;
	bool bUseDemo = false;
	double Start = FPlatformTime::Seconds();
	int32 BaseChunkZ = 0;
	const FGuid CollisionConsumer = FGuid::NewGuid();
	bool bHeightProbesRequested = false;
	bool bViewSet = false;
	bool bMovedToRemote = false;
	bool bEnding = false;
};

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FVoxelLandWorldConfigPlayTest, "VoxelLand.WorldConfig.Play",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FVoxelLandWorldConfigPlayTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	Names = {TEXT("Finite Height Rolling Hills"), TEXT("Infinite Height Rolling Hills"),
		TEXT("Finite Height Mountain Biome"), TEXT("Infinite Height Mountain Biome"), TEXT("Finite Direct Density"),
		TEXT("Infinite Direct Density")};
	Commands = Names;
}

bool FVoxelLandWorldConfigPlayTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Editor test map created"), FAutomationEditorCommonUtils::CreateNewMap()))
	{
		return false;
	}
	const bool bDirect = Parameters.Contains(TEXT("Direct Density"));
	const bool bInfinite = Parameters.StartsWith(TEXT("Infinite"));
	UVoxelLandWorldConfig* Config = nullptr;
	if (bDirect)
	{
		Config = NewObject<UVoxelLandWorldConfig>();
		Config->TerrainSource = EVoxelLandTerrainSource::DirectDensity;
		FVoxelLandConfigPlayCommand::MakeIsland(*Config);
	}
	else
	{
		const TCHAR* Path = Parameters.Contains(TEXT("Mountain Biome"))
							  ? TEXT("/VoxelLand/Examples/WC_MountainBiome.WC_MountainBiome")
							  : TEXT("/VoxelLand/Examples/WC_RollingHills.WC_RollingHills");
		const UVoxelLandWorldConfig* Asset = LoadObject<UVoxelLandWorldConfig>(nullptr, Path);
		if (!TestNotNull(TEXT("Shipped Height config loaded"), Asset))
		{
			return false;
		}
		Config = DuplicateObject<UVoxelLandWorldConfig>(Asset, GetTransientPackage());
	}
	if (!TestNotNull(TEXT("World Config prepared"), Config))
	{
		return false;
	}
	Config->WorldExtent = bInfinite ? EVoxelLandWorldExtent::Infinite : EVoxelLandWorldExtent::Finite;
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	const bool bUseDemo = Parameters == TEXT("Infinite Height Mountain Biome");
	FAutomationTestFramework::Get().EnqueueLatentCommand(
		MakeShared<FVoxelLandConfigPlayCommand>(*this, *Config, bUseDemo));
	return true;
}

#endif
