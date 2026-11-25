#include "Generation/Terrain/Shaping/VoxelLandSlope.h"

namespace VoxelLandM4Slope
{
class FVoxelLandM4SlopeInternals final
{
public:
	static constexpr int64 HeightLimitQ8 = 512000000;
	static constexpr int64 SlopeScale = 4096;
	static constexpr int64 SlopeLimitQ12 = 1048576;
	static int64 RoundAway(int64 Numerator, int64 Denominator);
	static int64 NearestRoot(int64 Value);
	static bool ValidHeight(int64 Value);
};

int64 FVoxelLandM4SlopeInternals::RoundAway(int64 Numerator, int64 Denominator)
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

int64 FVoxelLandM4SlopeInternals::NearestRoot(int64 Value)
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

bool FVoxelLandM4SlopeInternals::ValidHeight(int64 Value)
{
	return Value >= -HeightLimitQ8 && Value <= HeightLimitQ8;
}

bool DensityQ8(int64 ZQ8, int32 CenterQ8, int32 PlusXQ8, int32 MinusXQ8, int32 PlusYQ8, int32 MinusYQ8,
	int32 VoxelSizeCm, int64& OutDensityQ8)
{
	if (!FVoxelLandM4SlopeInternals::ValidHeight(ZQ8) || !FVoxelLandM4SlopeInternals::ValidHeight(CenterQ8)
		|| !FVoxelLandM4SlopeInternals::ValidHeight(PlusXQ8) || !FVoxelLandM4SlopeInternals::ValidHeight(MinusXQ8)
		|| !FVoxelLandM4SlopeInternals::ValidHeight(PlusYQ8) || !FVoxelLandM4SlopeInternals::ValidHeight(MinusYQ8)
		|| VoxelSizeCm <= 0 || VoxelSizeCm > 2000000)
	{
		return false;
	}
	const int64 StencilDenominator = int64(VoxelSizeCm) * 512;
	const int64 SlopeX = FVoxelLandM4SlopeInternals::RoundAway(
		(int64(PlusXQ8) - MinusXQ8) * FVoxelLandM4SlopeInternals::SlopeScale, StencilDenominator);
	const int64 SlopeY = FVoxelLandM4SlopeInternals::RoundAway(
		(int64(PlusYQ8) - MinusYQ8) * FVoxelLandM4SlopeInternals::SlopeScale, StencilDenominator);
	if (SlopeX < -FVoxelLandM4SlopeInternals::SlopeLimitQ12 || SlopeX > FVoxelLandM4SlopeInternals::SlopeLimitQ12
		|| SlopeY < -FVoxelLandM4SlopeInternals::SlopeLimitQ12 || SlopeY > FVoxelLandM4SlopeInternals::SlopeLimitQ12)
	{
		return false;
	}
	const int64 NormQ12 = FVoxelLandM4SlopeInternals::NearestRoot(
		FVoxelLandM4SlopeInternals::SlopeScale * FVoxelLandM4SlopeInternals::SlopeScale + SlopeX * SlopeX
		+ SlopeY * SlopeY);
	const int64 Density =
		FVoxelLandM4SlopeInternals::RoundAway((ZQ8 - CenterQ8) * FVoxelLandM4SlopeInternals::SlopeScale, NormQ12);
	OutDensityQ8 = Density;
	return true;
}
}
