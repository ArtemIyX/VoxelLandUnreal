#include "VoxelLandMaterialGraphFactory.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"

UVoxelLandMaterialGraphFactory::UVoxelLandMaterialGraphFactory()
{
	SupportedClass = UVoxelLandTerrainGraph::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

FText UVoxelLandMaterialGraphFactory::GetDisplayName() const
{
	return NSLOCTEXT("VoxelLand", "MaterialGraphFactory", "Material Graph");
}

UObject* UVoxelLandMaterialGraphFactory::FactoryCreateNew(
	UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags, UObject*, FFeedbackContext*)
{
	UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(Parent, InClass, Name, Flags | RF_Transactional);
	Graph->GraphRole = EVoxelLandGraphRole::Material;
	return Graph;
}
