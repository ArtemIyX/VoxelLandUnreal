#include "VoxelLandAssetFactories.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Assets/VoxelLandWorldSettings.h"
#include "Data/Assets/VoxelLandMaterialSetup.h"

UVoxelLandTerrainGraphFactory::UVoxelLandTerrainGraphFactory()
{
    SupportedClass = UVoxelLandTerrainGraph::StaticClass();
    bCreateNew = true;
    bEditAfterNew = true;
}
UObject* UVoxelLandTerrainGraphFactory::FactoryCreateNew(UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags, UObject*, FFeedbackContext*)
{
    return NewObject<UVoxelLandTerrainGraph>(Parent, InClass, Name, Flags | RF_Transactional);
}
UVoxelLandWorldSettingsFactory::UVoxelLandWorldSettingsFactory()
{
    SupportedClass = UVoxelLandWorldSettings::StaticClass();
    bCreateNew = true;
    bEditAfterNew = true;
}
UObject* UVoxelLandWorldSettingsFactory::FactoryCreateNew(UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags, UObject*, FFeedbackContext*)
{
    return NewObject<UVoxelLandWorldSettings>(Parent, InClass, Name, Flags | RF_Transactional);
}
UVoxelLandMaterialSetupFactory::UVoxelLandMaterialSetupFactory()
{
    SupportedClass = UVoxelLandMaterialSetup::StaticClass();
    bCreateNew = true;
    bEditAfterNew = true;
}
UObject* UVoxelLandMaterialSetupFactory::FactoryCreateNew(UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags, UObject*, FFeedbackContext*)
{
    return NewObject<UVoxelLandMaterialSetup>(Parent, InClass, Name, Flags | RF_Transactional);
}
