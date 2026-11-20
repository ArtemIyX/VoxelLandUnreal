#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandValueSource.h"

// Uniform parameter or constant bindings for the one-cell box filter.
struct FVoxelLandSmoothFilter
{
    bool bPresent = false;
    FVoxelLandValueSource Enabled;
    FVoxelLandValueSource Strength;
};
