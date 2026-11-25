#pragma once

#include "CoreTypes.h"

struct FVoxelLandM6RedistanceSettings
{
	static constexpr uint32 CurrentVersion = 1;
	static constexpr uint8 MaxBandVoxels = 2;

	uint32 Version = CurrentVersion;
	uint8 BandVoxels = 1;
	bool bEnabled = false;
};
