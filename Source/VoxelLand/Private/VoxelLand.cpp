// Copyright Epic Games, Inc. All Rights Reserved.

#include "VoxelLand.h"
#include "World/VoxelLandM4Preparation.h"

#define LOCTEXT_NAMESPACE "FVoxelLandModule"

void FVoxelLandModule::StartupModule()
{
	VoxelLandM4Preparation::StartCoordinator();
}

void FVoxelLandModule::ShutdownModule()
{
	// Actor jobs are stopped by their own world/actor teardown. The module
	// owns only the shared prepass coordinator and its producers.
	VoxelLandM4Preparation::ShutdownCoordinator();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FVoxelLandModule, VoxelLand)
