#pragma once

#include "CoreMinimal.h"

// Explicit M4 final filter settings. Version 1 is the existing deterministic
// one-cell Smooth Density algorithm; changing its semantics requires a new version.
struct FVoxelLandM4SmoothSettings
{
	uint32 Version = 1;
	bool bEnabled = false;
	float Strength = 0.f;
};
