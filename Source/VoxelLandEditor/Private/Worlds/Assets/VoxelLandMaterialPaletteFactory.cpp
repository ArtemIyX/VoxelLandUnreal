#include "VoxelLandMaterialPaletteFactory.h"

#include "Data/Assets/Materials/VoxelLandMaterialPalette.h"

UVoxelLandMaterialPaletteFactory::UVoxelLandMaterialPaletteFactory()
{
	SupportedClass = UVoxelLandMaterialPalette::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

FText UVoxelLandMaterialPaletteFactory::GetDisplayName() const
{
	return NSLOCTEXT("VoxelLand", "MaterialPaletteFactory", "Material Palette");
}

UObject* UVoxelLandMaterialPaletteFactory::FactoryCreateNew(
	UClass* InClass, UObject* Parent, const FName Name, const EObjectFlags Flags, UObject*, FFeedbackContext*)
{
	return NewObject<UVoxelLandMaterialPalette>(Parent, InClass, Name, Flags | RF_Transactional);
}
