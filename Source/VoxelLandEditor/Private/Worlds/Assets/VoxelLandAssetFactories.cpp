#include "VoxelLandAssetFactories.h"
#include "AssetToolsModule.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Assets/Graphs/VoxelLandGraphFunction.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Data/Assets/Materials/VoxelLandMaterialSetup.h"
#include "Data/Caves/Field/VoxelLandCaveProtectionAsset.h"

uint32 GetVoxelLandAssetMenuCategory()
{
	return FAssetToolsModule::GetModule().Get().FindAdvancedAssetCategory(TEXT("VoxelLand"));
}

uint32 UVoxelLandAssetFactory::GetMenuCategories() const
{
	return GetVoxelLandAssetMenuCategory();
}

FText UVoxelLandWorldConfigFactory::GetDisplayName() const
{
	return NSLOCTEXT("VoxelLand", "WorldConfigFactory", "World Config");
}

UVoxelLandTerrainGraphFactory::UVoxelLandTerrainGraphFactory()
{
	SupportedClass = UVoxelLandTerrainGraph::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UVoxelLandTerrainGraphFactory::FactoryCreateNew(
	UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags, UObject*, FFeedbackContext*)
{
	return NewObject<UVoxelLandTerrainGraph>(Parent, InClass, Name, Flags | RF_Transactional);
}

UVoxelLandGraphFunctionFactory::UVoxelLandGraphFunctionFactory()
{
	SupportedClass = UVoxelLandGraphFunction::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UVoxelLandGraphFunctionFactory::FactoryCreateNew(
	UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags, UObject*, FFeedbackContext*)
{
	return NewObject<UVoxelLandGraphFunction>(Parent, InClass, Name, Flags | RF_Transactional);
}

UVoxelLandWorldConfigFactory::UVoxelLandWorldConfigFactory()
{
	SupportedClass = UVoxelLandWorldConfig::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UVoxelLandWorldConfigFactory::FactoryCreateNew(
	UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags, UObject*, FFeedbackContext*)
{
	return NewObject<UVoxelLandWorldConfig>(Parent, InClass, Name, Flags | RF_Transactional);
}

UVoxelLandMaterialSetupFactory::UVoxelLandMaterialSetupFactory()
{
	SupportedClass = UVoxelLandMaterialSetup::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UVoxelLandMaterialSetupFactory::FactoryCreateNew(
	UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags, UObject*, FFeedbackContext*)
{
	return NewObject<UVoxelLandMaterialSetup>(Parent, InClass, Name, Flags | RF_Transactional);
}

UVoxelLandCaveProtectionFactory::UVoxelLandCaveProtectionFactory()
{
	SupportedClass = UVoxelLandM5ProtectionAsset::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UVoxelLandCaveProtectionFactory::FactoryCreateNew(
	UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags, UObject*, FFeedbackContext*)
{
	return NewObject<UVoxelLandM5ProtectionAsset>(Parent, InClass, Name, Flags | RF_Transactional);
}

FText UVoxelLandTerrainGraphFactory::GetDisplayName() const
{
	return NSLOCTEXT("VoxelLand", "TerrainGraphFactory", "Terrain Graph");
}

FText UVoxelLandGraphFunctionFactory::GetDisplayName() const
{
	return NSLOCTEXT("VoxelLand", "GraphFunctionFactory", "Graph Function");
}

FText UVoxelLandMaterialSetupFactory::GetDisplayName() const
{
	return NSLOCTEXT("VoxelLand", "MaterialSetupFactory", "Material Setup");
}

FText UVoxelLandCaveProtectionFactory::GetDisplayName() const
{
	return NSLOCTEXT("VoxelLand", "CaveProtectionFactory", "Cave Protection");
}
