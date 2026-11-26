#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Assets/Materials/VoxelLandMaterialPalette.h"
#include "Data/Graph/Schema/VoxelLandBuiltinNode.h"
#include "Data/Meshing/VoxelLandWorldSignalSnapshot.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Data/Assets/Materials/VoxelLandMaterialSetup.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Rendering/Components/VoxelLandChunkComponent.h"
#include "Rendering/Materials/VoxelLandTerrainMaterial.h"
#include "World/VoxelLandWorld.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "RHI.h"
#include "SceneTypes.h"
#include "UObject/StrongObjectPtr.h"

class FVoxelLandTerrainMaterialWorldModesCommand final : public IAutomationLatentCommand
{
public:
	FVoxelLandTerrainMaterialWorldModesCommand(FAutomationTestBase& InTest, UWorld& InWorld, AVoxelLandWorld& InActor)
		: Test(InTest)
		, World(InWorld)
		, Actor(InActor)
	{
	}

	bool BeginMode()
	{
		static constexpr const TCHAR* ConfigPaths[] = {TEXT("/VoxelLand/Examples/WC_MountainBiome.WC_MountainBiome"),
			TEXT("/VoxelLand/Examples/TerrainMaterials/WC_InfiniteMountainBiome.WC_InfiniteMountainBiome"),
			TEXT("/VoxelLand/Examples/TerrainMaterials/WC_DensityMaterials.WC_DensityMaterials"),
			TEXT("/VoxelLand/Examples/TerrainMaterials/WC_DensityMaterials.WC_DensityMaterials")};
		const UVoxelLandWorldConfig* Source = LoadObject<UVoxelLandWorldConfig>(nullptr, ConfigPaths[Mode]);
		if (!Test.TestNotNull(TEXT("Shipped World Config required for material runtime smoke"), Source))
		{
			return false;
		}
		Config.Reset(DuplicateObject<UVoxelLandWorldConfig>(Source, GetTransientPackage()));
		bUseProceduralMaterials = FParse::Param(FCommandLine::Get(), TEXT("VoxelLandProceduralMaterialWorldModes"));
		if (bUseProceduralMaterials)
		{
			const UVoxelLandWorldConfig* Grass = LoadObject<UVoxelLandWorldConfig>(
				nullptr, TEXT("/VoxelLand/Examples/TerrainMaterials/WC_GrassPatches.WC_GrassPatches"));
			if (!Test.TestNotNull(TEXT("Grass Patches configuration"), Grass))
			{
				return false;
			}
			if (!ConfigureProceduralAppearance(*Grass))
			{
				return false;
			}
		}
		Config->WorldExtent = Mode == FiniteHeightMode || Mode == FiniteDensityMode ? EVoxelLandWorldExtent::Finite
																					: EVoxelLandWorldExtent::Infinite;
		Config->TerrainAmbientOcclusionStrength =
			FParse::Param(FCommandLine::Get(), TEXT("VoxelLandOutdoorLightingWorldModes")) ? 0.75f : 0.0f;
		Config->HorizontalRadius = 1;
		Config->VerticalRadius = 2;
		Actor.bEnableM7Clipmap = Mode < HeightModeCount;
		if (Actor.bEnableM7Clipmap)
		{
			Config->HeightScaleCm = 600;
			Config->BaseElevationCm = 600.f;
		}
		else
		{
			Config->DensityBounds.MinChunk = FIntVector(-4);
			Config->DensityBounds.MaxChunkExclusive = FIntVector(4);
		}
		bHasRequestedView = false;
		bCheckingSupersession = false;
		bRefreshedSignals = false;
		PreviousTicket = 0;
		Deadline = FPlatformTime::Seconds() + ModeTimeoutSeconds;
		return Test.TestTrue(FString::Printf(TEXT("%s configuration initializes"), ModeLabel()),
			Actor.InitializeFromConfig(Config.Get(), FIntVector::ZeroValue));
	}

	bool Update() override
	{
		Actor.Tick(1.f / 60.f);
		if (FPlatformTime::Seconds() > Deadline)
		{
			Test.AddError(FString::Printf(TEXT("%s terrain material publication timed out. %s %s"), ModeLabel(),
				*Actor.LastInitializationError, *Actor.M7ClipmapError));
			return Finish();
		}
		if (Actor.bInitializing)
		{
			return false;
		}
		if (bCheckingSupersession)
		{
			return CheckSupersededRules();
		}
		if (!Actor.HasWorldStateForTests() || !Actor.LastInitializationError.IsEmpty())
		{
			Test.AddError(FString::Printf(
				TEXT("%s world failed to initialize. %s"), ModeLabel(), *Actor.LastInitializationError));
			return Finish();
		}
		if (!bHasRequestedView)
		{
			bHasRequestedView = true;
			Actor.SetTestViewLocation(FVector(1600.0, 1600.0, 1600.0));
			if (Mode == FiniteDensityMode)
			{
				Test.TestFalse(TEXT("Finite Direct Density declines a material chunk request outside its bounds"),
					Actor.RequestDataChunk(FIntVector(4, 0, 0)));
			}
			return false;
		}
		const uint64 Ticket = Actor.GetM7PublishedTicketForTests();
		if (Actor.bEnableM7Clipmap && (Ticket == 0 || Ticket == PreviousTicket))
		{
			return false;
		}
		const int32 VisibleCount = CheckVisibleComponents();
		if (VisibleCount == 0)
		{
			return false;
		}
		Test.AddInfo(
			FString::Printf(TEXT("%s published %d material-bound visible components."), ModeLabel(), VisibleCount));
		if (Mode == InfiniteHeightMode && PreviousTicket == 0)
		{
			PreviousTicket = Ticket;
			Actor.SetTestViewLocation(FVector(14400.0, 1600.0, 1600.0));
			return false;
		}
		if (Mode == InfiniteHeightMode)
		{
			Test.TestTrue(
				TEXT("Moving the view publishes a new material-bound clipmap revision"), Ticket > PreviousTicket);
		}
		if (bUseProceduralMaterials && !bRefreshedSignals)
		{
			const TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Data =
				Actor.FindChunk(FVoxelLandChunkKey{});
			{
				const TGuardValue<int64> ChangedSeed(Config->Seed, Config->Seed + 1);
				FString Error;
				Test.TestFalse(TEXT("Reject signal refresh after accepted terrain inputs change"),
					Actor.SetMaterialWorldSignals(nullptr, Error));
				Test.TestTrue(TEXT("Report changed terrain inputs"), Error.Contains(TEXT("settings changed")));
			}
			if (!SetGrassSignal(1.0, 2))
			{
				return Finish();
			}
			Test.TestTrue(TEXT("Replacing a material world map preserves resident density"),
				Actor.FindChunk(FVoxelLandChunkKey{}) == Data);
			bRefreshedSignals = true;
			PreviousTicket = Ticket;
			return false;
		}
		if (!bUseProceduralMaterials && (Mode == FiniteHeightMode || Mode == FiniteDensityMode))
		{
			return BeginRuleSupersession();
		}
		return AdvanceMode();
	}

	bool Finish()
	{
		Actor.SetPresentationHookForTests({});
		Actor.Shutdown();
		Config.Reset();
		World.DestroyWorld(false);
		GEngine->DestroyWorldContext(&World);
		World.RemoveFromRoot();
		return true;
	}

private:
	bool ConfigureProceduralAppearance(const UVoxelLandWorldConfig& Grass)
	{
		UVoxelLandTerrainGraph* Graph = Grass.MaterialGraph.LoadSynchronous();
		if (!Test.TestNotNull(TEXT("Editable Grass Patches material graph"), Graph))
		{
			return false;
		}
		MaterialGraph.Reset(DuplicateObject(Graph, GetTransientPackage()));
		FVoxelLandGraphNodeRecord* Noise =
			MaterialGraph->Nodes.FindByPredicate([](const FVoxelLandGraphNodeRecord& Node) {
				return Node.OperatorId == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::WorldNoise2D);
			});
		if (!Test.TestNotNull(TEXT("Graph noise source to replace with a World Signal"), Noise))
		{
			return false;
		}
		const FVoxelLandGraphPinRecord Result = Noise->Pins.Last();
		FVoxelLandGraphPinRecord Default;
		Default.Id = FGuid::NewGuid();
		Default.Type = EVoxelLandGraphValueType::Float;
		Default.FloatDefault = 0.0f;
		FVoxelLandGraphPinRecord Required;
		Required.Id = FGuid::NewGuid();
		Required.Type = EVoxelLandGraphValueType::Bool;
		Required.BoolDefault = true;
		Noise->Pins = {Default, Required, Result};
		Noise->OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::WorldSignal);
		const FTCHARToUTF8 Channel(TEXT("GrassMoisture"));
		Noise->Configuration.Reset();
		Noise->Configuration.Append(reinterpret_cast<const uint8*>(Channel.Get()), Channel.Length());
		Config->MaterialSetup = Grass.MaterialSetup;
		Config->MaterialGraph = MaterialGraph.Get();
		return SetGrassSignal(-1.0, 1);
	}

	bool SetGrassSignal(double Value, uint64 Revision)
	{
		FVoxelLandWorldSignalGrid Grid;
		Grid.Name = TEXT("GrassMoisture");
		Grid.Size = FIntPoint(2, 2);
		Grid.OriginCm = FVector2d(-100000.0, -100000.0);
		Grid.SpacingCm = 200000.0;
		Grid.Samples.Init(Value, 4);
		FVoxelLandWorldSignalSnapshot::FPtr Signals;
		FString Error;
		const bool bApplied = FVoxelLandWorldSignalSnapshot::Create({MoveTemp(Grid)}, Revision, Signals, Error)
						   && Actor.SetMaterialWorldSignals(Signals, Error);
		return Test.TestTrue(Error.IsEmpty() ? TEXT("Supply the immutable grass map") : *Error, bApplied);
	}

	bool BeginRuleSupersession()
	{
		const UVoxelLandMaterialSetup* Original = Config->MaterialSetup.Get();
		if (!Test.TestTrue(
				TEXT("Saved example supplies editable material rules"), Original && !Original->Rules.IsEmpty()))
		{
			return Finish();
		}
		MutableSetup.Reset(DuplicateObject<UVoxelLandMaterialSetup>(Original, GetTransientPackage()));
		Config->MaterialSetup = MutableSetup.Get();
		PriorMaterial.Reset(Actor.TerrainMaterial);
		PriorData = Actor.FindChunk(FVoxelLandChunkKey{});
		PriorClipmapTicket = Actor.GetM7PublishedTicketForTests();
		bCheckingSupersession = true;
		bRuleChangedInHook = false;
		Actor.SetPresentationHookForTests([this] { ChangeRuleDuringPresentation(); });
		Deadline = FPlatformTime::Seconds() + ModeTimeoutSeconds;
		if (!Test.TestTrue(TEXT("Replacement initialization begins before editing the appearance rules"),
				Actor.InitializeFromConfig(Config.Get(), FIntVector::ZeroValue)))
		{
			return Finish();
		}
		return false;
	}

	void ChangeRuleDuringPresentation()
	{
		constexpr double ThresholdChangeCm = 625.0;
		MutableSetup->Rules[0].Threshold += ThresholdChangeCm;
		bRuleChangedInHook = true;
	}

	bool CheckSupersededRules()
	{
		Test.TestTrue(TEXT("The test edited an appearance rule after resolving presentation"), bRuleChangedInHook);
		Test.TestTrue(TEXT("The stale presentation result is rejected"), !Actor.LastInitializationError.IsEmpty());
		Test.TestTrue(TEXT("The previously published palette instance remains active"),
			Actor.TerrainMaterial == PriorMaterial.Get());
		Test.TestTrue(TEXT("The previously published terrain stays in place"),
			Actor.HasWorldStateForTests() && Actor.GetM7PublishedTicketForTests() == PriorClipmapTicket
				&& (!PriorData || Actor.FindChunk(FVoxelLandChunkKey{}) == PriorData));
		return AdvanceMode();
	}

	bool AdvanceMode()
	{
		Actor.Shutdown();
		PriorData.Reset();
		PriorMaterial.Reset();
		MutableSetup.Reset();
		++Mode;
		return Mode >= ModeCount || !BeginMode() ? Finish() : false;
	}

	int32 CheckVisibleComponents()
	{
		int32 Count = 0;
		bool bAllSharePalette = true;
		bool bAllHaveLogicalOrigin = true;
		bool bAllHaveExpectedVisibility = true;
		bool bAllCastShadows = true;
		for (TActorIterator<AVoxelLandRegion> It(&World); It; ++It)
		{
			if (It->IsHidden())
			{
				continue;
			}
			TInlineComponentArray<UVoxelLandChunkComponent*> Components;
			It->GetComponents(Components);
			for (const UVoxelLandChunkComponent* Component : Components)
			{
				if (Component->IsRegistered() && Component->IsVisible())
				{
					++Count;
					bAllCastShadows &= Component->CastShadow;
					bAllSharePalette = bAllSharePalette && Component->GetMaterial(0) == Actor.TerrainMaterial;
					bAllHaveLogicalOrigin = bAllHaveLogicalOrigin && HasLogicalOrigin(*Component);
					bAllHaveExpectedVisibility &=
						Component->HasAmbientVisibilityForTests() == (Config->TerrainAmbientOcclusionStrength > 0.0f);
				}
			}
		}
		if (Count > 0)
		{
			const UVoxelLandMaterialSetup* Setup = Config->MaterialSetup.Get();
			const UVoxelLandMaterialPalette* Palette = Setup ? Setup->Palette.Get() : nullptr;
			UMaterialInstanceDynamic* const Instance = Cast<UMaterialInstanceDynamic>(Actor.TerrainMaterial);
			Test.TestTrue(TEXT("Visible chunks share the instance of the expected baked palette master"),
				bAllSharePalette && Instance && Palette && Palette->MasterMaterial
					&& Instance->GetMaterial() == Palette->MasterMaterial->GetMaterial());
			Test.TestTrue(
				TEXT("Every published packed component receives its logical texture origin"), bAllHaveLogicalOrigin);
			Test.TestTrue(TEXT("Geometry visibility is present only when requested"), bAllHaveExpectedVisibility);
			Test.TestTrue(TEXT("Streamed terrain casts raster shadows"), bAllCastShadows);
			if (Instance)
			{
				Test.TestEqual(TEXT("Terrain material receives the configured AO strength"),
					Instance->K2_GetScalarParameterValue(FVoxelLandTerrainMaterial::AmbientOcclusionStrengthParameter),
					Config->TerrainAmbientOcclusionStrength);
			}
		}
		return Count;
	}

	static bool HasLogicalOrigin(const UVoxelLandChunkComponent& Component)
	{
		const TArray<float>& Data = Component.GetCustomPrimitiveData().Data;
		constexpr int32 High = FVoxelLandTerrainMaterial::LogicalOriginHighIndex;
		constexpr int32 Low = FVoxelLandTerrainMaterial::LogicalOriginLowIndex;
		if (Data.Num() < Low + 4)
		{
			return false;
		}
		const FVector3d Origin(double(Data[High]) + Data[Low], double(Data[High + 1]) + Data[Low + 1],
			double(Data[High + 2]) + Data[Low + 2]);
		return Origin.Equals(Component.GetComponentLocation());
	}

	const TCHAR* ModeLabel() const
	{
		static constexpr const TCHAR* Labels[] = {TEXT("Finite Height"), TEXT("Infinite Height"),
			TEXT("Finite Direct Density"), TEXT("Infinite Direct Density")};
		return Labels[Mode];
	}

	static constexpr int32 ModeCount = 4;
	static constexpr int32 HeightModeCount = 2;
	static constexpr int32 FiniteHeightMode = 0;
	static constexpr int32 InfiniteHeightMode = 1;
	static constexpr int32 FiniteDensityMode = 2;
	static constexpr double ModeTimeoutSeconds = 25.0;
	FAutomationTestBase& Test;
	UWorld& World;
	AVoxelLandWorld& Actor;
	TStrongObjectPtr<UVoxelLandWorldConfig> Config;
	TStrongObjectPtr<UVoxelLandMaterialSetup> MutableSetup;
	TStrongObjectPtr<UVoxelLandTerrainGraph> MaterialGraph;
	TStrongObjectPtr<UMaterialInterface> PriorMaterial;
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> PriorData;
	uint64 PriorClipmapTicket = 0;
	int32 Mode = 0;
	uint64 PreviousTicket = 0;
	double Deadline = 0.0;
	bool bHasRequestedView = false;
	bool bCheckingSupersession = false;
	bool bRuleChangedInHook = false;
	bool bUseProceduralMaterials = false;
	bool bRefreshedSignals = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandTerrainMaterialWorldModesTest, "VoxelLand.Materials.WorldModes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandTerrainMaterialWorldModesTest::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender() || GUsingNullRHI || GMaxRHIShaderPlatform != SP_PCD3D_SM6)
	{
		AddWarning(TEXT("Terrain material world presentation requires D3D12 SM6 and commandlet rendering."));
		return true;
	}
	if (!TestNotNull(TEXT("UE engine available"), GEngine))
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World modes fixture created a game world"), World))
	{
		return false;
	}
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	AVoxelLandWorld* Actor = World->SpawnActor<AVoxelLandWorld>();
	if (!TestNotNull(TEXT("World modes fixture spawned a terrain actor"), Actor))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		return false;
	}
	Actor->WorkerCount = 2;
	Actor->M7MaxLOD = 1;
	Actor->M7MaxLeaves = 64;
	Actor->M7HorizontalRadiusCm = 1600.0;
	Actor->M7VerticalRadiusCm = 1700.0;
	Actor->DispatchBeginPlay();
	const TSharedRef<FVoxelLandTerrainMaterialWorldModesCommand> Command =
		MakeShared<FVoxelLandTerrainMaterialWorldModesCommand>(*this, *World, *Actor);
	if (!Command->BeginMode())
	{
		Command->Finish();
		return false;
	}
	FAutomationTestFramework::Get().EnqueueLatentCommand(Command);
	return true;
}

#endif
