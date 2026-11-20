// Copyright Epic Games, Inc. All Rights Reserved.

#include "VoxelLand.h"
#include "World/VoxelLandWorld.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "FVoxelLandModule"

void FVoxelLandModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
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
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FVoxelLandModule, VoxelLand)