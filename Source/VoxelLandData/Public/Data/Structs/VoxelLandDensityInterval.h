#pragma once
#include "CoreMinimal.h"

// A proven inclusive binary32 interval; unknown never permits classification.
struct FVoxelLandDensityInterval
{
    float Minimum = 0.f;
    float Maximum = 0.f;
    bool bKnown = false;
};
