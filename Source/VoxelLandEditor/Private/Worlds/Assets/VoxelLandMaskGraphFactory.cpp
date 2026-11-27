#include "VoxelLandMaskGraphFactory.h"
#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"

UVoxelLandMaskGraphFactory::UVoxelLandMaskGraphFactory()
{
	SupportedClass = UVoxelLandTerrainGraph::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

FText UVoxelLandMaskGraphFactory::GetDisplayName() const
{
	return NSLOCTEXT("VoxelLand", "MaskGraphFactory", "Mask Graph");
}

UObject* UVoxelLandMaskGraphFactory::FactoryCreateNew(
	UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags, UObject*, FFeedbackContext*)
{
	UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(Parent, InClass, Name, Flags | RF_Transactional);
	Graph->GraphRole = EVoxelLandGraphRole::Mask;
	return Graph;
}
