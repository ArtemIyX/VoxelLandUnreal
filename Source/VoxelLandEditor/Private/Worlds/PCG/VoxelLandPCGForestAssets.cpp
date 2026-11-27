#if WITH_DEV_AUTOMATION_TESTS

#include "VoxelLandPCGForestExample.h"

#include "../../Graph/Toolkit/VoxelLandGraphToolkit.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "VoxelLandPCGWorld.h"
#include "Worlds/VoxelLandTerrainPreview.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/LightComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PCGGraph.h"
#include "UObject/SavePackage.h"

class FVoxelLandPCGForestAssets final
{
public:
	static constexpr const TCHAR* Root = TEXT("/VoxelLand/Examples/PCGForest/");
	static constexpr const TCHAR* MaskName = TEXT("MG_Forest");
	static constexpr const TCHAR* GraphName = TEXT("PCG_Forest");
	static constexpr const TCHAR* FiniteName = TEXT("WC_ForestFinite");
	static constexpr const TCHAR* InfiniteName = TEXT("WC_ForestInfinite");
	static constexpr const TCHAR* PreviewMapName = TEXT("ForestPreview");
	static constexpr const TCHAR* InfiniteMapName = TEXT("ForestInfinite");
	static constexpr const TCHAR* TemplatePath = TEXT("/VoxelLand/Examples/WC_RollingHills.WC_RollingHills");

	static UPackage* MakePackage(const TCHAR* Name) { return CreatePackage(*(FString(Root) + Name)); }

	static bool Save(UObject& Asset, const bool bMap)
	{
		UPackage* Package = Asset.GetOutermost();
		FAssetRegistryModule::AssetCreated(&Asset);
		Asset.MarkPackageDirty();
		const FString Extension =
			bMap ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), Extension);
		const FString Directory = FPaths::GetPath(Filename);
		if (!IFileManager::Get().DirectoryExists(*Directory) && !IFileManager::Get().MakeDirectory(*Directory, true))
		{
			return false;
		}
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Package, &Asset, *Filename, Args);
	}

	static bool HasExistingAssets()
	{
		for (const TCHAR* Name : {MaskName, GraphName, FiniteName, InfiniteName, PreviewMapName, InfiniteMapName})
		{
			if (FPackageName::DoesPackageExist(FString(Root) + Name))
			{
				return true;
			}
		}
		return false;
	}

	static UVoxelLandWorldConfig* MakeConfig(const UVoxelLandWorldConfig& Template, UVoxelLandTerrainGraph& Mask,
		const TCHAR* Name, const EVoxelLandWorldExtent Extent)
	{
		UVoxelLandWorldConfig* Config =
			DuplicateObject<UVoxelLandWorldConfig>(&Template, MakePackage(Name), FName(Name));
		Config->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
		Config->WorldExtent = Extent;
		Config->MaskGraph = &Mask;
		Config->bGlobalErosion = false;
		return Config;
	}

	static void LayoutMask(UVoxelLandTerrainGraph& Mask)
	{
		const TSharedRef<FVoxelLandGraphToolkit> Toolkit = MakeShared<FVoxelLandGraphToolkit>();
		Toolkit->InitializeForTests(&Mask);
		if (Mask.EditorGraph)
		{
			const FIntPoint Positions[] = {{0, 0}, {240, 0}, {480, 0}, {240, 240}, {480, 240}, {720, 0}, {960, 0}};
			for (UEdGraphNode* Node : Mask.EditorGraph->Nodes)
			{
				const int32 Index = Mask.Nodes.IndexOfByPredicate(
					[Node](const FVoxelLandGraphNodeRecord& Record) { return Record.Id == Node->NodeGuid; });
				if (Index >= 0 && Index < UE_ARRAY_COUNT(Positions))
				{
					Node->NodePosX = Positions[Index].X;
					Node->NodePosY = Positions[Index].Y;
				}
			}
		}
		Toolkit->Stop();
	}

	static bool BuildMap(const TCHAR* Name, UVoxelLandWorldConfig& Config, UPCGGraph& Graph, const bool bPreview)
	{
		const UWorld::InitializationValues Init = UWorld::InitializationValues()
													  .CreatePhysicsScene(false)
													  .CreateNavigation(false)
													  .CreateAISystem(false)
													  .ShouldSimulatePhysics(false);
		UWorld* World = UWorld::CreateWorld(
			EWorldType::Editor, false, FName(Name), MakePackage(Name), false, ERHIFeatureLevel::Num, &Init);
		if (!World)
		{
			return false;
		}
		World->SetFlags(RF_Public | RF_Standalone);
		if (bPreview)
		{
			AVoxelLandTerrainPreview* Preview = World->SpawnActor<AVoxelLandTerrainPreview>();
			if (!Preview || !FVoxelLandPCGForestExample::ConfigureLayer(*Preview->Placement, Graph))
			{
				return false;
			}
			Preview->WorldConfig = &Config;
			Preview->SetActorLabel(TEXT("Forest · Editor Preview"));
		}
		AVoxelLandPCGWorld* Runtime = World->SpawnActor<AVoxelLandPCGWorld>();
		if (!Runtime || !FVoxelLandPCGForestExample::ConfigureLayer(*Runtime->Placement, Graph))
		{
			return false;
		}
		Runtime->Config = &Config;
		Runtime->SetActorLabel(TEXT("Forest · Runtime Terrain and PCG"));
		World->SpawnActor<APlayerStart>(FVector(-3400.0, -3400.0, 2500.0), FRotator(-22.0, 45.0, 0.0));
		if (ADirectionalLight* Sun =
				World->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-40.0, -30.0, 0.0)))
		{
			Sun->GetLightComponent()->SetIntensity(5.0f);
			Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		}
		return Save(*World, true);
	}
};

bool FVoxelLandPCGForestExample::WriteAssets(FString& OutError)
{
	using FAssets = FVoxelLandPCGForestAssets;
	if (FAssets::HasExistingAssets())
	{
		OutError = TEXT("The forest examples already exist. Use a clean checkout to generate the original assets.");
		return false;
	}
	UVoxelLandWorldConfig* Template = LoadObject<UVoxelLandWorldConfig>(nullptr, FAssets::TemplatePath);
	if (!Template)
	{
		OutError = TEXT("The Rolling Hills template is missing.");
		return false;
	}
	constexpr EObjectFlags AssetFlags = RF_Public | RF_Standalone | RF_Transactional;
	UVoxelLandTerrainGraph* Mask = NewObject<UVoxelLandTerrainGraph>(
		FAssets::MakePackage(FAssets::MaskName), FName(FAssets::MaskName), AssetFlags);
	const FGuid MaskId = BuildMask(*Mask);
	FAssets::LayoutMask(*Mask);
	UPCGGraph* Graph =
		NewObject<UPCGGraph>(FAssets::MakePackage(FAssets::GraphName), FName(FAssets::GraphName), AssetFlags);
	if (!BuildGraph(*Graph, MaskId))
	{
		OutError = TEXT("Could not wire the forest point graph.");
		return false;
	}
	UVoxelLandWorldConfig* Finite =
		FAssets::MakeConfig(*Template, *Mask, FAssets::FiniteName, EVoxelLandWorldExtent::Finite);
	UVoxelLandWorldConfig* Infinite =
		FAssets::MakeConfig(*Template, *Mask, FAssets::InfiniteName, EVoxelLandWorldExtent::Infinite);
	if (!FAssets::Save(*Mask, false) || !FAssets::Save(*Graph, false) || !FAssets::Save(*Finite, false)
		|| !FAssets::Save(*Infinite, false) || !FAssets::BuildMap(FAssets::PreviewMapName, *Finite, *Graph, true)
		|| !FAssets::BuildMap(FAssets::InfiniteMapName, *Infinite, *Graph, false))
	{
		OutError = TEXT("The forest example could not save every asset and map.");
		return false;
	}
	return true;
}

#endif
