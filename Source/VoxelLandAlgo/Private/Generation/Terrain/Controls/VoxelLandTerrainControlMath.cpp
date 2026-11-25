#include "Generation/Terrain/Controls/VoxelLandTerrainControlMath.h"

#include <cmath>
#include <limits>

namespace VoxelLandM4Control
{
bool Quantize(float Value, uint16& OutValue)
{
	if (!FMath::IsFinite(Value))
	{
		return false;
	}
	const double Clamped = FMath::Clamp(double(Value), -1.0, 1.0);
	const double Scaled = (Clamped + 1.0) * 32767.5;
	OutValue = uint16(std::floor(Scaled + 0.5));
	return true;
}

bool LatticePositionCm(int64 VoxelIndex, int64 VoxelSizeCm, int64& OutWorldCm)
{
	if (VoxelSizeCm <= 0 || VoxelIndex > std::numeric_limits<int64>::max() / VoxelSizeCm
		|| VoxelIndex < std::numeric_limits<int64>::min() / VoxelSizeCm)
	{
		return false;
	}
	OutWorldCm = VoxelIndex * VoxelSizeCm;
	return true;
}

bool GridCoordinate(int64 WorldCm, int64 OriginCm, int64 SpacingCm, int64& OutIndex, int64& OutRemainderCm)
{
	if (SpacingCm <= 0 || (OriginCm > 0 && WorldCm < std::numeric_limits<int64>::min() + OriginCm)
		|| (OriginCm < 0 && WorldCm > std::numeric_limits<int64>::max() + OriginCm))
	{
		return false;
	}
	const int64 OffsetCm = WorldCm - OriginCm;
	int64 Index = OffsetCm / SpacingCm;
	int64 Remainder = OffsetCm % SpacingCm;
	if (Remainder < 0)
	{
		--Index;
		Remainder += SpacingCm;
	}
	OutIndex = Index;
	OutRemainderCm = Remainder;
	return true;
}

bool BilinearHeightQ8(
	int32 H00, int32 H10, int32 H01, int32 H11, int64 RemainderX, int64 RemainderY, int64 SpacingCm, int32& OutHeightQ8)
{
	constexpr int64 MaxHeightQ8 = 512000000;
	constexpr int64 MaxInt64 = std::numeric_limits<int64>::max();
	if (SpacingCm <= 0 || RemainderX < 0 || RemainderY < 0 || RemainderX > SpacingCm || RemainderY > SpacingCm
		|| int64(H00) < -MaxHeightQ8 || int64(H00) > MaxHeightQ8 || int64(H10) < -MaxHeightQ8
		|| int64(H10) > MaxHeightQ8 || int64(H01) < -MaxHeightQ8 || int64(H01) > MaxHeightQ8
		|| int64(H11) < -MaxHeightQ8 || int64(H11) > MaxHeightQ8 || SpacingCm > MaxInt64 / SpacingCm)
	{
		return false;
	}
	const int64 Denominator = SpacingCm * SpacingCm;
	if (Denominator > MaxInt64 / MaxHeightQ8)
	{
		return false;
	}
	// The nonnegative weights sum to Denominator. Therefore the absolute sum
	// of all terms is at most MaxHeightQ8 * Denominator, even when signs differ.
	const int64 X0 = SpacingCm - RemainderX;
	const int64 Y0 = SpacingCm - RemainderY;
	const int64 Numerator = int64(H00) * X0 * Y0 + int64(H10) * RemainderX * Y0 + int64(H01) * X0 * RemainderY
						  + int64(H11) * RemainderX * RemainderY;
	int64 Rounded = Numerator / Denominator;
	const int64 Remainder = Numerator % Denominator;
	const int64 Magnitude = Remainder < 0 ? -Remainder : Remainder;
	if (Magnitude != 0 && Magnitude >= Denominator - Magnitude)
	{
		Rounded += Numerator < 0 ? -1 : 1;
	}
	OutHeightQ8 = int32(Rounded);
	return true;
}
}