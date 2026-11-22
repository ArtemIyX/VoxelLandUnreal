#include "Generation/VoxelLandM5CheckedMath.h"

#include <limits>

namespace VoxelLand::M5::CheckedMath
{
bool TryAdd(int64 A, int64 B, int64& Out)
{
	constexpr int64 Min = std::numeric_limits<int64>::min();
	constexpr int64 Max = std::numeric_limits<int64>::max();
	if ((B > 0 && A > Max - B) || (B < 0 && A < Min - B))
	{
		return false;
	}
	Out = A + B;
	return true;
}

bool TrySubtract(int64 A, int64 B, int64& Out)
{
	constexpr int64 Min = std::numeric_limits<int64>::min();
	constexpr int64 Max = std::numeric_limits<int64>::max();
	if ((B > 0 && A < Min + B) || (B < 0 && A > Max + B))
	{
		return false;
	}
	Out = A - B;
	return true;
}

bool TryMultiply(int64 A, int64 B, int64& Out)
{
	constexpr int64 Min = std::numeric_limits<int64>::min();
	constexpr int64 Max = std::numeric_limits<int64>::max();
	if (A > 0)
	{
		if ((B > 0 && A > Max / B) || (B < 0 && B < Min / A))
		{
			return false;
		}
	}
	else if (A < 0)
	{
		if ((B > 0 && A < Min / B) || (B < 0 && A < Max / B))
		{
			return false;
		}
	}
	Out = A * B;
	return true;
}

bool TryMultiplyNonNegative(int64 A, int64 B, int64& Out)
{
	return A >= 0 && B >= 0 && TryMultiply(A, B, Out);
}

bool TryFloorDivide(int64 Value, int64 PositiveDivisor, int64& Out)
{
	if (PositiveDivisor <= 0)
	{
		return false;
	}
	const int64 Quotient = Value / PositiveDivisor;
	const int64 Remainder = Value % PositiveDivisor;
	// A nonzero remainder means the divisor is greater than one, so the
	// quotient is strictly above MIN_int64 and decrementing it is safe.
	Out = Remainder < 0 ? Quotient - 1 : Quotient;
	return true;
}
}
