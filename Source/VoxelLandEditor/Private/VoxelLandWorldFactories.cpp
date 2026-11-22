#include "VoxelLandFiniteWorldFactory.h"
#include "VoxelLandInfiniteWorldFactory.h"

#include "Data/Assets/VoxelLandFiniteWorld.h"
#include "Data/Assets/VoxelLandInfiniteWorld.h"

UVoxelLandFiniteWorldFactory::UVoxelLandFiniteWorldFactory()
{
	SupportedClass = UVoxelLandFiniteWorld::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UVoxelLandFiniteWorldFactory::FactoryCreateNew(
	UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags, UObject*, FFeedbackContext*)
{
	if (!Parent || !InClass || !InClass->IsChildOf(UVoxelLandFiniteWorld::StaticClass())
		|| InClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return nullptr;
	}
	return NewObject<UVoxelLandFiniteWorld>(Parent, InClass, Name, Flags | RF_Transactional);
}

UVoxelLandInfiniteWorldFactory::UVoxelLandInfiniteWorldFactory()
{
	SupportedClass = UVoxelLandInfiniteWorld::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UVoxelLandInfiniteWorldFactory::FactoryCreateNew(
	UClass* InClass, UObject* Parent, FName Name, EObjectFlags Flags, UObject*, FFeedbackContext*)
{
	if (!Parent || !InClass || !InClass->IsChildOf(UVoxelLandInfiniteWorld::StaticClass())
		|| InClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return nullptr;
	}
	return NewObject<UVoxelLandInfiniteWorld>(Parent, InClass, Name, Flags | RF_Transactional);
}
