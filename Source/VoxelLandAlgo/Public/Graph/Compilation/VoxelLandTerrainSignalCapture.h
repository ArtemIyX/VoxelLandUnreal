#pragma once

#include "Data/Field/Chunks/VoxelLandGeneratorSettings.h"
#include "Data/Graph/Sources/VoxelLandGraphSnapshot.h"

struct FVoxelLandTerrainSignalCapture
{
	FVoxelLandGraphSnapshot Graph;
	FVoxelLandGeneratorSettings Settings;
	FString GraphPath;
	bool bUseFixedLattice = false;
	bool bUseThreeDimensions = false;
};
