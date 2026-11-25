#pragma once

#include "CoreMinimal.h"

class FVoxelLandQuantizedDensity final
{
public:
	static int32 RoundRatioAwayFromZero(int64 Numerator, int32 Denominator);
	static int16 Blend(int16 Original, int32 Target, int32 Strength);
	static int16 SmoothSample(
		TConstArrayView<int16> Expanded, const FIntVector& ExpandedSize, int32 X, int32 Y, int32 Z, int32 Strength);
};
