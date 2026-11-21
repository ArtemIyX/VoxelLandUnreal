#include "Generation/VoxelLandM4Slope.h"

namespace VoxelLandM4Slope
{
namespace
{
constexpr int64 HeightLimitQ8 = 512000000;
constexpr int64 SlopeScale = 4096;
constexpr int64 SlopeLimitQ12 = 1048576;

int64 RoundAway(int64 Numerator, int64 Denominator)
{
	// Validated operands exclude INT64_MIN. Remainder comparison avoids
	// overflow from adding half the denominator to the numerator.
	const int64 Magnitude = Numerator < 0 ? -Numerator : Numerator;
	int64 Result = Magnitude / Denominator;
	if (Magnitude % Denominator >= (Denominator + 1) / 2)
	{
		++Result;
	}
	return Numerator < 0 ? -Result : Result;
}

int64 NearestRoot(int64 Value)
{
	// Integer binary search; upper bound includes the maximum allowed norm.
	int64 Low = 0;
	int64 High = 2097153;
	while (Low + 1 < High)
	{
		const int64 Mid = Low + (High - Low) / 2;
		if (Mid * Mid <= Value)
		{
			Low = Mid;
		}
		else
		{
			High = Mid;
		}
	}
	return Value - Low * Low > Low ? Low + 1 : Low;
}

bool ValidHeight(int64 Value)
{
	return Value >= -HeightLimitQ8 && Value <= HeightLimitQ8;
}
}

bool DensityQ8(int64 ZQ8, int32 CenterQ8, int32 PlusXQ8, int32 MinusXQ8, int32 PlusYQ8, int32 MinusYQ8,
	int32 VoxelSizeCm, int64& OutDensityQ8)
{
	if (!ValidHeight(ZQ8) || !ValidHeight(CenterQ8) || !ValidHeight(PlusXQ8) || !ValidHeight(MinusXQ8)
		|| !ValidHeight(PlusYQ8) || !ValidHeight(MinusYQ8) || VoxelSizeCm <= 0 || VoxelSizeCm > 2000000)
	{
		return false;
	}
	const int64 StencilDenominator = int64(VoxelSizeCm) * 512;
	const int64 SlopeX = RoundAway((int64(PlusXQ8) - MinusXQ8) * SlopeScale, StencilDenominator);
	const int64 SlopeY = RoundAway((int64(PlusYQ8) - MinusYQ8) * SlopeScale, StencilDenominator);
	if (SlopeX < -SlopeLimitQ12 || SlopeX > SlopeLimitQ12 || SlopeY < -SlopeLimitQ12 || SlopeY > SlopeLimitQ12)
	{
		return false;
	}
	const int64 NormQ12 = NearestRoot(SlopeScale * SlopeScale + SlopeX * SlopeX + SlopeY * SlopeY);
	const int64 Density = RoundAway((ZQ8 - CenterQ8) * SlopeScale, NormQ12);
	OutDensityQ8 = Density;
	return true;
}
}
