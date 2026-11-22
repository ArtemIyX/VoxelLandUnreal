#pragma once

#include "CoreMinimal.h"

// Observed ranges from the exact control and base-height samples used to
// produce one immutable finite prepass. Controls are normalized uint16;
// heights are signed Q8 centimeters. Not a persistent world format.
struct FVoxelLandM4StageStats
{
	static constexpr int32 ControlMapResolution = 32;
	// Fixed, absolute 0..255 samples of the pre-erosion
	// uint16 controls; no full control grids are retained for inspection.
	bool bHasControlMaps = false;
	uint8 ControlMaps[3][ControlMapResolution * ControlMapResolution] = {};
	int32 SampleCount = 0;
	uint16 ContinentalMin = 0;
	uint16 ContinentalMax = 0;
	uint16 MountainMin = 0;
	uint16 MountainMax = 0;
	uint16 PeaksMin = 0;
	uint16 PeaksMax = 0;
	uint64 ContinentalSum = 0;
	uint64 MountainSum = 0;
	uint64 PeaksSum = 0;
	int32 ContinentalZeroCount = 0;
	int32 ContinentalMaxCount = 0;
	int32 MountainZeroCount = 0;
	int32 MountainMaxCount = 0;
	int32 PeaksZeroCount = 0;
	int32 PeaksMaxCount = 0;
	// Difference between final and base height, including unchanged samples.
	int32 ErosionChangedCount = 0;
	int64 ErosionDeltaMinQ8 = 0;
	int64 ErosionDeltaMaxQ8 = 0;
	uint64 ErosionAbsDeltaSumQ8 = 0;
	int32 BaseHeightMinQ8 = 0;
	int32 BaseHeightMaxQ8 = 0;
};
