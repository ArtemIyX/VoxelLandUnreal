#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandM4DomainMode.h"

// Standalone proposed M4 policy; not yet attached to world settings.
// The finite defaults cover 16.384 km square about the world origin.
struct FVoxelLandM4DomainSettings
{
	EVoxelLandM4DomainMode Mode = EVoxelLandM4DomainMode::Finite;
	bool bGlobalErosion = true;
	int64 OriginXCm = -819200;
	int64 OriginYCm = -819200;
	int32 CellsX = 256;
	int32 CellsY = 256;
	int64 GridSpacingCm = 6400;
};
