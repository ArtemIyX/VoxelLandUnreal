#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM4OverhangConfig.h"
#include "Data/Structs/VoxelLandM4HeightDetailConfig.h"
#include "Data/Structs/VoxelLandM4SmoothSettings.h"

class FVoxelLandGenerator;
class FVoxelLandM4CurveLut;

// Immutable-by-copy inputs for the no-global-erosion, locally evaluated mode.
// The three control generators must be ordinary proven-2D VM generators.
struct FVoxelLandM4UnlimitedConfig
{
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> ContinentalGenerator;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> MountainGenerator;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> PeaksGenerator;
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> ContinentalCurve;
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> MountainCurve;
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> PeaksCurve;
	int32 BaseQ8 = 0;
	bool bNormalizeSlope = false;
	FVoxelLandM4OverhangConfig Overhang;
	FVoxelLandM4HeightDetailConfig HeightDetail;
	// Unset preserves the final program's legacy filter behavior.
	TOptional<FVoxelLandM4SmoothSettings> SmoothOverride;
};
