// Copyright Epic Games, Inc. All Rights Reserved.

#include "VoxelLand.h"
#include "World/VoxelLandWorld.h"
#include "World/VoxelLandM4Preparation.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "FVoxelLandModule"

void FVoxelLandModule::StartupModule()
{
	VoxelLandM4Preparation::StartCoordinator();
}

void FVoxelLandModule::ShutdownModule()
{
	// No worker or queued callback may retain module code across unloading.
	for (TObjectIterator<AVoxelLandWorld> It; It; ++It)
	{
		if (It->PendingInitialization || It->PendingDemo || It->State)
		{
			It->Shutdown();
		}
	}
	VoxelLandM4Preparation::ShutdownCoordinator();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FVoxelLandModule, VoxelLand)
