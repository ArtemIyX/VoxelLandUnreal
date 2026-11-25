#include "VoxelLandFiniteWorldAssetActions.h"
#include "VoxelLandInfiniteWorldAssetActions.h"

#include "Data/Assets/Worlds/VoxelLandFiniteWorld.h"
#include "Data/Assets/Worlds/VoxelLandInfiniteWorld.h"
#include "Toolkits/SimpleAssetEditor.h"

#define LOCTEXT_NAMESPACE "VoxelLandWorldAssetActions"

FText FVoxelLandFiniteWorldAssetActions::GetName() const
{
	return LOCTEXT("FiniteWorld", "VoxelLand Finite World");
}

FColor FVoxelLandFiniteWorldAssetActions::GetTypeColor() const
{
	return FColor(92, 158, 94);
}

UClass* FVoxelLandFiniteWorldAssetActions::GetSupportedClass() const
{
	return UVoxelLandFiniteWorld::StaticClass();
}

uint32 FVoxelLandFiniteWorldAssetActions::GetCategories()
{
	return EAssetTypeCategories::Misc;
}

void FVoxelLandFiniteWorldAssetActions::OpenAssetEditor(const TArray<UObject*>& Objects, TSharedPtr<IToolkitHost> Host)
{
	TArray<UObject*> Worlds;
	for (UObject* Object : Objects)
	{
		if (IsValid(Object) && Object->IsA<UVoxelLandFiniteWorld>())
		{
			Worlds.Add(Object);
		}
	}
	if (!Worlds.IsEmpty())
	{
		FSimpleAssetEditor::CreateEditor(
			Host.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone, Host, Worlds);
	}
}

FText FVoxelLandInfiniteWorldAssetActions::GetName() const
{
	return LOCTEXT("InfiniteWorld", "VoxelLand Infinite World");
}

FColor FVoxelLandInfiniteWorldAssetActions::GetTypeColor() const
{
	return FColor(83, 145, 190);
}

UClass* FVoxelLandInfiniteWorldAssetActions::GetSupportedClass() const
{
	return UVoxelLandInfiniteWorld::StaticClass();
}

uint32 FVoxelLandInfiniteWorldAssetActions::GetCategories()
{
	return EAssetTypeCategories::Misc;
}

void FVoxelLandInfiniteWorldAssetActions::OpenAssetEditor(
	const TArray<UObject*>& Objects, TSharedPtr<IToolkitHost> Host)
{
	TArray<UObject*> Worlds;
	for (UObject* Object : Objects)
	{
		if (IsValid(Object) && Object->IsA<UVoxelLandInfiniteWorld>())
		{
			Worlds.Add(Object);
		}
	}
	if (!Worlds.IsEmpty())
	{
		FSimpleAssetEditor::CreateEditor(
			Host.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone, Host, Worlds);
	}
}

#undef LOCTEXT_NAMESPACE
