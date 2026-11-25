#pragma once

#include "CoreMinimal.h"
#include "VoxelLandTerrainSmoothingSettings.generated.h"

// Explicit M4 final filter settings. Version 1 is the existing deterministic
// one-cell Smooth Density algorithm; changing its semantics requires a new version.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM4SmoothSettings
{
	GENERATED_BODY()
	UPROPERTY()
	uint32 Version = 1;
	UPROPERTY()
	bool bEnabled = false;
	UPROPERTY()
	float Strength = 0.f;
};
