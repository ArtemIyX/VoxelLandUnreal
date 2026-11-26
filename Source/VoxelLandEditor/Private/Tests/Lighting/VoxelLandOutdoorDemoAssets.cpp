#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "World/Demo/VoxelLandClipmapDemo.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Camera/CameraActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

class FVoxelLandOutdoorDemoBuilder final
{
public:
	static bool Build(FAutomationTestBase& Test)
	{
		const FString MapFilename =
			FPackageName::LongPackageNameToFilename(MapPath, FPackageName::GetMapPackageExtension());
		const FString ConfigFilename =
			FPackageName::LongPackageNameToFilename(ConfigPath, FPackageName::GetAssetPackageExtension());
		IFileManager& Files = IFileManager::Get();
		if (Files.FileExists(*MapFilename) || Files.FileExists(*ConfigFilename))
		{
			Test.AddError(TEXT("The outdoor demo output already exists. Existing editable assets were left intact."));
			return false;
		}

		TStrongObjectPtr<UVoxelLandWorldConfig> Template(LoadObject<UVoxelLandWorldConfig>(
			nullptr, TEXT("/VoxelLand/Examples/TerrainMaterials/WC_MountainGrassPatches.WC_MountainGrassPatches")));
		if (!Test.TestNotNull(TEXT("Mountain Grass Patches template"), Template.Get()) || Template->IsDirectDensity())
		{
			return false;
		}
		const FString Directory = FPaths::GetPath(MapFilename);
		if (!Test.TestTrue(TEXT("Create outdoor demo content directory"),
				Files.DirectoryExists(*Directory) || Files.MakeDirectory(*Directory, true)))
		{
			return false;
		}

		TStrongObjectPtr<UVoxelLandWorldConfig> Config(DuplicateObject<UVoxelLandWorldConfig>(
			Template.Get(), CreatePackage(ConfigPath), TEXT("WC_OutdoorLighting")));
		Config->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
		Config->WorldExtent = EVoxelLandWorldExtent::Infinite;
		Config->bGlobalErosion = false;
		Config->HeightScaleCm = 2400.0f;
		Config->BaseElevationCm = 600.0f;
		Config->TerrainAmbientOcclusionStrength = 0.75f;

		UWorld::InitializationValues Init;
		Init.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
		TStrongObjectPtr<UWorld> World(UWorld::CreateWorld(EWorldType::Editor, false, TEXT("OutdoorLighting"),
			CreatePackage(MapPath), false, ERHIFeatureLevel::Num, &Init));
		if (!Test.TestNotNull(TEXT("Outdoor demo world"), World.Get()) || !Populate(Test, *World.Get(), *Config.Get()))
		{
			return false;
		}
		World->SetFlags(RF_Public | RF_Standalone);
		World->GetOutermost()->SetPackageFlags(PKG_ContainsMap);

		return Test.TestTrue(TEXT("Save outdoor lighting World Config"), Save(*Config.Get(), ConfigFilename))
			&& Test.TestTrue(TEXT("Save playable outdoor lighting level"), Save(*World.Get(), MapFilename));
	}

private:
	static constexpr const TCHAR* MapPath = TEXT("/VoxelLand/Examples/OutdoorLighting/OutdoorLighting");
	static constexpr const TCHAR* ConfigPath = TEXT("/VoxelLand/Examples/OutdoorLighting/WC_OutdoorLighting");

	template <typename ActorType>
	static ActorType* Spawn(UWorld& World, const TCHAR* Label, const FVector& Position = FVector::ZeroVector,
		const FRotator& Rotation = FRotator::ZeroRotator)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ActorType* Actor = World.SpawnActor<ActorType>(Position, Rotation, Params);
		if (Actor)
		{
			Actor->SetActorLabel(Label);
		}
		return Actor;
	}

	static bool Populate(FAutomationTestBase& Test, UWorld& World, UVoxelLandWorldConfig& Config)
	{
		AVoxelLandClipmapDemo* Terrain = Spawn<AVoxelLandClipmapDemo>(World, TEXT("Outdoor Terrain"));
		const FObjectProperty* TerrainConfig =
			FindFProperty<FObjectProperty>(AVoxelLandClipmapDemo::StaticClass(), TEXT("TerrainConfig"));
		if (!Test.TestNotNull(TEXT("Clipmap demo actor"), Terrain)
			|| !Test.TestNotNull(TEXT("Clipmap demo World Config property"), TerrainConfig))
		{
			return false;
		}
		TerrainConfig->SetObjectPropertyValue_InContainer(Terrain, &Config);
		Terrain->M7MaxLOD = 1;
		Terrain->M7HorizontalRadiusCm = 4800.0;
		Terrain->M7VerticalRadiusCm = 3800.0;
		Terrain->M7MaxLeaves = 256;

		const FVector StartPosition(-3100.0, -2100.0, 2600.0);
		const FRotator StartRotation(-20.0, 35.0, 0.0);
		APlayerStart* Player = Spawn<APlayerStart>(World, TEXT("Flight Start"), StartPosition, StartRotation);
		ACameraActor* EntryCamera = Spawn<ACameraActor>(World, TEXT("Entry Camera"), StartPosition, StartRotation);
		ACameraActor* RidgeCamera = Spawn<ACameraActor>(
			World, TEXT("Ridge Camera"), FVector(-3000.0, -2500.0, 4300.0), FRotator(-18.0, 55.0, 0.0));
		ACameraActor* OverlookCamera = Spawn<ACameraActor>(
			World, TEXT("Overlook Camera"), FVector(11000.0, 8500.0, 13500.0), FRotator(-35.0, -140.0, 0.0));
		if (!Test.TestTrue(
				TEXT("Add player and reference camera poses"), Player && EntryCamera && RidgeCamera && OverlookCamera))
		{
			return false;
		}
		World.GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();

		ADirectionalLight* Sun =
			Spawn<ADirectionalLight>(World, TEXT("Movable Sun"), FVector::ZeroVector, FRotator(-18.0, -45.0, 0.0));
		ASkyLight* Sky = Spawn<ASkyLight>(World, TEXT("Movable Sky Light"));
		ASkyAtmosphere* Atmosphere = Spawn<ASkyAtmosphere>(World, TEXT("Sky Atmosphere"));
		APostProcessVolume* PostProcess = Spawn<APostProcessVolume>(World, TEXT("Reference Exposure"));
		UTextureCube* Cubemap = LoadObject<UTextureCube>(
			nullptr, TEXT("/Engine/MapTemplates/Sky/SunsetAmbientCubemap.SunsetAmbientCubemap"));
		if (!Test.TestTrue(TEXT("Add sun, sky and reference exposure settings"),
				Sun && Sky && Atmosphere && PostProcess && Cubemap))
		{
			return false;
		}

		UDirectionalLightComponent* SunComponent = Sun->GetComponent();
		SunComponent->SetMobility(EComponentMobility::Movable);
		SunComponent->SetCastShadows(true);
		SunComponent->SetCastRaytracedShadows(ECastRayTracedShadow::Disabled);
		SunComponent->SetIntensity(16.f);
		SunComponent->SetAtmosphereSunLight(true);

		USkyLightComponent* SkyComponent = Sky->GetLightComponent();
		SkyComponent->SetMobility(EComponentMobility::Movable);
		SkyComponent->SourceType = SLS_SpecifiedCubemap;
		SkyComponent->SetCubemap(Cubemap);
		SkyComponent->SetIntensity(4.f);

		PostProcess->bUnbound = true;
		FPostProcessSettings& Exposure = PostProcess->Settings;
		Exposure.bOverride_AutoExposureMethod = true;
		Exposure.AutoExposureMethod = AEM_Manual;
		Exposure.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		Exposure.AutoExposureApplyPhysicalCameraExposure = false;
		Exposure.bOverride_AutoExposureBias = true;
		Exposure.AutoExposureBias = 2.f;
		Exposure.bOverride_LocalExposureHighlightContrastScale = true;
		Exposure.LocalExposureHighlightContrastScale = 1.f;
		Exposure.bOverride_LocalExposureShadowContrastScale = true;
		Exposure.LocalExposureShadowContrastScale = 1.f;
		Exposure.bOverride_DynamicGlobalIlluminationMethod = true;
		Exposure.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
		Exposure.bOverride_ReflectionMethod = true;
		Exposure.ReflectionMethod = EReflectionMethod::None;
		return true;
	}

	static bool Save(UObject& Asset, const FString& Filename)
	{
		UPackage* Package = Asset.GetOutermost();
		Package->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Package, &Asset, *Filename, Args))
		{
			return false;
		}
		FAssetRegistryModule::AssetCreated(&Asset);
		return true;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandCreateOutdoorDemoTest, "VoxelLand.Lighting.CreateOutdoorDemo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandCreateOutdoorDemoTest::RunTest(const FString& Parameters)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("VoxelLandCreateOutdoorDemo")))
	{
		return true;
	}
	return FVoxelLandOutdoorDemoBuilder::Build(*this);
}

#endif
