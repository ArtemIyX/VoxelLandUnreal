#if WITH_DEV_AUTOMATION_TESTS

#include "../../Worlds/Assets/VoxelLandAssetFactories.h"
#include "../../Worlds/Assets/VoxelLandMaterialPaletteFactory.h"
#include "../../Worlds/Assets/VoxelLandSurfaceMaterialFactory.h"
#include "Data/Assets/Materials/VoxelLandMaterialPalette.h"
#include "Data/Assets/Materials/VoxelLandSurfaceMaterial.h"
#include "Data/Assets/Graphs/VoxelLandGraphFunction.h"
#include "Data/Assets/Materials/VoxelLandMaterialSetup.h"
#include "Data/Caves/Field/VoxelLandCaveProtectionAsset.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "AssetToolsModule.h"
#include "AssetTypeCategories.h"
#include "IAssetTypeActions.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandWorldAssetEditorTest, "VoxelLand.WorldModes.Editor.AssetTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLandWorldAssetEditorTest::RunTest(const FString& Parameters)
{
	UVoxelLandWorldConfigFactory* Factory = NewObject<UVoxelLandWorldConfigFactory>();
	UPackage* Package = GetTransientPackage();
	const FName Name = MakeUniqueObjectName(Package, UVoxelLandWorldConfig::StaticClass());
	UVoxelLandWorldConfig* Config = Cast<UVoxelLandWorldConfig>(
		Factory->FactoryCreateNew(UVoxelLandWorldConfig::StaticClass(), Package, Name, RF_Transient, nullptr, nullptr));
	if (!TestNotNull(TEXT("Factory creates the unified world config"), Config))
	{
		return false;
	}
	TestEqual(TEXT("Default terrain source"), Config->TerrainSource, EVoxelLandTerrainSource::HeightTerrain);
	TestEqual(TEXT("Default world extent"), Config->WorldExtent, EVoxelLandWorldExtent::Finite);
	TestTrue(TEXT("World config uses dedicated factory"), Factory->ShouldShowInNewMenu());
	TestTrue(TEXT("World config is hidden from the generic class picker"),
		UVoxelLandWorldConfig::StaticClass()->HasAnyClassFlags(CLASS_HideDropDown));
	TestTrue(
		TEXT("World config stays out of Miscellaneous"), Factory->GetMenuCategories() != EAssetTypeCategories::Misc);
	const uint32 Category = Factory->GetMenuCategories();
	const TPair<UFactory*, UClass*> Entries[] = {
		{NewObject<UVoxelLandTerrainGraphFactory>(), UVoxelLandTerrainGraph::StaticClass()},
		{NewObject<UVoxelLandGraphFunctionFactory>(), UVoxelLandGraphFunction::StaticClass()},
		{Factory, UVoxelLandWorldConfig::StaticClass()},
		{NewObject<UVoxelLandMaterialSetupFactory>(), UVoxelLandMaterialSetup::StaticClass()},
		{NewObject<UVoxelLandCaveProtectionFactory>(), UVoxelLandM5ProtectionAsset::StaticClass()},
		{NewObject<UVoxelLandSurfaceMaterialFactory>(), UVoxelLandSurfaceMaterial::StaticClass()},
		{NewObject<UVoxelLandMaterialPaletteFactory>(), UVoxelLandMaterialPalette::StaticClass()}};
	const TCHAR* Labels[] = {TEXT("Terrain Graph"), TEXT("Graph Function"), TEXT("World Config"),
		TEXT("Material Setup"), TEXT("Cave Protection"), TEXT("Surface Material"), TEXT("Material Palette")};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Entries); ++Index)
	{
		UFactory* Entry = Entries[Index].Key;
		UClass* Type = Entries[Index].Value;
		const FString Label(Labels[Index]);
		TestEqual(TEXT("Exact asset menu label"), Entry->GetDisplayName().ToString(), Label);
		TestEqual(*Label, Entry->GetMenuCategories(), Category);
		TestEqual(*Label, Entry->GetSupportedClass(), Type);
		TestTrue(*Label, Entry->ShouldShowInNewMenu());
		TestTrue(*Label, Type->HasAnyClassFlags(CLASS_HideDropDown));
		const TSharedPtr<IAssetTypeActions> Actions =
			FAssetToolsModule::GetModule().Get().GetAssetTypeActionsForClass(Type).Pin();
		TestTrue(TEXT("Content Browser type matches the creation label"),
			Actions.IsValid() && Actions->GetName().ToString() == Label);
	}
	return true;
}

#endif
