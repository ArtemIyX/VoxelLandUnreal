#pragma once

#include "CoreMinimal.h"

/** Parameters for the standalone, version-1 overhang displacement kernel.
 * Distances are in 1/256 cm. This record does not enable an M4 world stage.
 */
struct VOXELLANDDATA_API FVoxelLandM4OverhangSettings
{
	bool bEnabled = false;
	int32 AmplitudeQ8 = 0;
	int32 BandHalfWidthQ8 = 256000;
};
