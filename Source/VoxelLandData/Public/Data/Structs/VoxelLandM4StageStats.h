#pragma once

#include "CoreMinimal.h"

// Observed ranges from the exact control and base-height samples used to
// produce one immutable finite prepass. Controls are normalized uint16;
// heights are signed Q8 centimeters. Not a persistent world format.
struct FVoxelLandM4StageStats
{
	int32 SampleCount = 0;
	uint16 ContinentalMin = 0;
	uint16 ContinentalMax = 0;
	uint16 MountainMin = 0;
	uint16 MountainMax = 0;
	uint16 PeaksMin = 0;
	uint16 PeaksMax = 0;
	int32 BaseHeightMinQ8 = 0;
	int32 BaseHeightMaxQ8 = 0;
};
