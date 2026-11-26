#include "Materials/VoxelLandTerrainMaterialBuilder.h"

#include "Data/Assets/Materials/VoxelLandMaterialPalette.h"
#include "Rendering/Materials/VoxelLandTerrainMaterial.h"
#include "Rendering/Components/VoxelLandChunkComponent.h"
#include "Engine/Texture2DArray.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "RHI.h"
#include "ShaderCompiler.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandTerrainMaterialLibraryTest, "VoxelLand.Materials.MasterMaterial",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandTerrainMaterialLibraryTest::RunTest(const FString& Parameters)
{
	UVoxelLandMaterialPalette* Palette = NewObject<UVoxelLandMaterialPalette>();
	Palette->BakedSlots.AddDefaulted(2);
	Palette->BakedSlots[0].TilePeriodCm = 125.0;
	Palette->BakedSlots[1].TilePeriodCm = 470.5;
	Palette->BakedSlots[1].Tint = FLinearColor(0.75f, 0.5f, 0.25f, 1.0f);
	Palette->BakedSlots[1].HeightBlendContrast = 0.625f;
	const FLinearColor Periods = FVoxelLandTerrainMaterial::PeriodGroup(*Palette, 0);
	TestEqual(TEXT("Independent periods and unused channels retain the fallback period"), Periods,
		FLinearColor(125.0f, 470.5f, 125.0f, 125.0f));
	TestEqual(TEXT("Tint and height contrast occupy one unsampled uniform"),
		FVoxelLandTerrainMaterial::SlotTintContrast(*Palette, 1), FLinearColor(0.75f, 0.5f, 0.25f, 0.625f));

	constexpr int32 FixtureSize = 4;
	constexpr int32 FixtureLayers = 2;
	Palette->BaseColorArray = UTexture2DArray::CreateTransient(FixtureSize, FixtureSize, FixtureLayers);
	Palette->NormalArray = UTexture2DArray::CreateTransient(FixtureSize, FixtureSize, FixtureLayers);
	Palette->ORMHArray = UTexture2DArray::CreateTransient(FixtureSize, FixtureSize, FixtureLayers);
	Palette->NormalArray->SRGB = false;
	Palette->ORMHArray->SRGB = false;

	UMaterial* Master = FVoxelLandTerrainMaterialBuilder::Build(Palette, TEXT("TerrainMaterialTest"), *Palette);
	if (!TestNotNull(TEXT("Master material builds from three array resources"), Master))
	{
		return false;
	}

	TArray<FMaterialParameterInfo> TextureParameters;
	TArray<FGuid> TextureParameterIds;
	Master->GetAllTextureParameterInfo(TextureParameters, TextureParameterIds);
	TestEqual(TEXT("Only base color, normal and ORMH are bound as textures"), TextureParameters.Num(), 3);
	Palette->MasterMaterial = Master;
	TestNotNull(TEXT("Shared render instance accepts the generated master"),
		FVoxelLandTerrainMaterial::CreateInstance(Palette, *Palette));

	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("Shader compilation coverage requires a rendering RHI."));
		return true;
	}

	UMaterial* RenderMaterial = FVoxelLandTerrainMaterialBuilder::LoadSharedMaster();
	if (!RenderMaterial)
	{
		RenderMaterial = Master;
	}
	RenderMaterial->CacheShaders(EMaterialShaderPrecompileMode::Default);
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
	const FMaterialResource* Resource = RenderMaterial->GetMaterialResource(GMaxRHIShaderPlatform);
	if (!TestNotNull(TEXT("Master has a resource for the active RHI"), Resource))
	{
		return false;
	}
	for (const FString& Error : Resource->GetCompileErrors())
	{
		AddError(Error);
	}
	TestTrue(
		TEXT("Shader compiler finished the active material shader map"), Resource->IsGameThreadShaderMapComplete());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandTerrainLogicalOriginTest, "VoxelLand.Materials.LogicalOrigin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandTerrainLogicalOriginTest::RunTest(const FString& Parameters)
{
	UVoxelLandChunkComponent* Component = NewObject<UVoxelLandChunkComponent>();
	const FVector3d Origin(-2147000123.125, 2147000521.625, -1000402.0625);
	FVoxelLandTerrainMaterial::SetLogicalOrigin(*Component, Origin);
	const TArray<float>& Data = Component->GetCustomPrimitiveData().Data;
	const int32 LowIndex = FVoxelLandTerrainMaterial::LogicalOriginLowIndex;
	if (!TestTrue(TEXT("Both high and low position uniforms are stored"), Data.Num() >= LowIndex + 4))
	{
		return false;
	}
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const double Reconstructed =
			double(Data[FVoxelLandTerrainMaterial::LogicalOriginHighIndex + Axis]) + double(Data[LowIndex + Axis]);
		TestEqual(
			TEXT("A negative or positive large origin retains fractional centimeters"), Reconstructed, Origin[Axis]);
	}

	FVoxelLandTerrainMaterial::SetLogicalOrigin(*Component, FVector3d::ZeroVector);
	TestEqual(TEXT("Reused component clears the previous high part"), Data[0], 0.0f);
	TestEqual(TEXT("Reused component clears the previous low part"), Data[LowIndex], 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandTerrainMaterialCreateMasterTest, "VoxelLand.Materials.CreateMaster",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandTerrainMaterialCreateMasterTest::RunTest(const FString& Parameters)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("VoxelLandCreateTerrainMaster")))
	{
		AddInfo(TEXT("Shared master creation requires -VoxelLandCreateTerrainMaster."));
		return true;
	}
	if (FVoxelLandTerrainMaterialBuilder::LoadSharedMaster())
	{
		AddInfo(TEXT("The shared terrain master material already exists."));
		return true;
	}

	UMaterial* Material = FVoxelLandTerrainMaterialBuilder::CreateSharedMaster();
	if (!TestNotNull(TEXT("Create shared terrain master material"), Material))
	{
		return false;
	}

	UPackage* Package = Material->GetOutermost();
	const FString Filename =
		FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	const FString Directory = FPaths::GetPath(Filename);
	if (!IFileManager::Get().DirectoryExists(*Directory) && !IFileManager::Get().MakeDirectory(*Directory, true))
	{
		AddError(FString::Printf(TEXT("Cannot create shared material directory %s."), *Directory));
		return false;
	}

	FAssetRegistryModule::AssetCreated(Material);
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	return TestTrue(TEXT("Persist shared master and its neutral array defaults"),
		UPackage::SavePackage(Package, Material, *Filename, SaveArgs));
}

#endif
