#include "Generation/VoxelLandM4Overhang.h"

#include <cmath>
#include <limits>

namespace VoxelLandM4Overhang
{
class FVoxelLandM4OverhangInternals final
{
public:
	static constexpr int32 MaxAmplitudeQ8 = 128000;
	static constexpr int32 MaxBandQ8 = 1280000;
	static constexpr int32 NoiseScale = 32767;
};

bool IsValid(const FVoxelLandM4OverhangSettings& Settings)
{
	return Settings.AmplitudeQ8 >= 0 && Settings.AmplitudeQ8 <= FVoxelLandM4OverhangInternals::MaxAmplitudeQ8
		&& Settings.BandHalfWidthQ8 >= 1 && Settings.BandHalfWidthQ8 <= FVoxelLandM4OverhangInternals::MaxBandQ8
		&& Settings.AmplitudeQ8 <= Settings.BandHalfWidthQ8;
}

bool QuantizeNoise(float Noise, int32& OutNoise)
{
	if (!FMath::IsFinite(Noise))
	{
		return false;
	}
	const double Clamped = FMath::Clamp(double(Noise), -1.0, 1.0);
	const int32 Magnitude = int32(std::floor(std::abs(Clamped) * FVoxelLandM4OverhangInternals::NoiseScale + 0.5));
	OutNoise = Clamped < 0.0 ? -Magnitude : Magnitude;
	return true;
}

bool DisplacementQ8(
	const FVoxelLandM4OverhangSettings& Settings, int64 HeightDifferenceQ8, int32 NoiseQ, int32& OutDeltaQ8)
{
	if (!IsValid(Settings) || NoiseQ < -FVoxelLandM4OverhangInternals::NoiseScale
		|| NoiseQ > FVoxelLandM4OverhangInternals::NoiseScale)
	{
		return false;
	}
	const int64 Band = Settings.BandHalfWidthQ8;
	// Compare before negation so even INT64_MIN is safe.
	if (!Settings.bEnabled || Settings.AmplitudeQ8 == 0 || HeightDifferenceQ8 <= -Band || HeightDifferenceQ8 >= Band
	    || NoiseQ == 0)
	{
		OutDeltaQ8 = 0;
		return true;
	}
	const int64 AbsHeight = HeightDifferenceQ8 < 0 ? -HeightDifferenceQ8 : HeightDifferenceQ8;
	const int64 Weight = Band - AbsHeight;
	const int64 AbsNoise = NoiseQ < 0 ? -int64(NoiseQ) : int64(NoiseQ);
	// Bounds above imply numerator <= 128000 * 1280000 * 32767 < INT64_MAX.
	// Keep explicit checks so a later limit change cannot introduce overflow.
	const int64 Max = std::numeric_limits<int64>::max();
	if (int64(Settings.AmplitudeQ8) > Max / Weight)
	{
		return false;
	}
	const int64 WeightedAmplitude = int64(Settings.AmplitudeQ8) * Weight;
	if (AbsNoise > 0 && WeightedAmplitude > Max / AbsNoise)
	{
		return false;
	}
	const int64 Numerator = WeightedAmplitude * AbsNoise;
	if (Band > Max / FVoxelLandM4OverhangInternals::NoiseScale)
	{
		return false;
	}
	const int64 Denominator = Band * FVoxelLandM4OverhangInternals::NoiseScale;
	// Quotient and remainder avoid a potential overflow from numerator + denominator/2.
	int64 Rounded = Numerator / Denominator;
	const int64 Remainder = Numerator % Denominator;
	if (Remainder >= (Denominator + 1) / 2)
	{
		++Rounded;
	}
	if (Rounded > FVoxelLandM4OverhangInternals::MaxAmplitudeQ8)
	{
		return false;
	}
	OutDeltaQ8 = NoiseQ < 0 ? -int32(Rounded) : int32(Rounded);
	return true;
}
}