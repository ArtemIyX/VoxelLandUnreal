#include "VoxelLandSurfaceMaterialFactory.h"

#include "Data/Assets/Materials/VoxelLandSurfaceMaterial.h"

UVoxelLandSurfaceMaterialFactory::UVoxelLandSurfaceMaterialFactory()
{
	SupportedClass = UVoxelLandSurfaceMaterial::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

FText UVoxelLandSurfaceMaterialFactory::GetDisplayName() const
{
	return NSLOCTEXT("VoxelLand", "SurfaceMaterialFactory", "Surface Material");
}

UObject* UVoxelLandSurfaceMaterialFactory::FactoryCreateNew(
	UClass* InClass, UObject* Parent, const FName Name, const EObjectFlags Flags, UObject*, FFeedbackContext*)
{
	return NewObject<UVoxelLandSurfaceMaterial>(Parent, InClass, Name, Flags | RF_Transactional);
}
