#include "Generation/VoxelLandPower.h"
#include <limits>

static_assert(sizeof(float) == 4 && sizeof(double) == 8 &&
              std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559,
	"VoxelLand Power requires IEEE-754 binary32 and binary64");

class FVoxelLandPowerInternals final
{
public:
	static double PowerOfTwo(int32 Exponent);
	static void Decompose(uint32 Magnitude, double& Mantissa, int32& Exponent);
	static double Decode(uint32 Magnitude);
	static double Log2Mantissa(double Mantissa);
	static double Exp2Fraction(double Fraction);
	static uint32 RoundEven(double Value);
	static bool PackResult(double Log2Result, bool bNegative, float& Result);
};

// Inputs and outputs use IEEE binary32, and intermediates IEEE binary64. Construct
// powers of two directly so subnormal inputs are not subject to flush-to-zero.
double FVoxelLandPowerInternals::PowerOfTwo(int32 Exponent)
{
	const uint64 Bits = uint64(Exponent + 1023) << 52;
	double Value;
	FMemory::Memcpy(&Value, &Bits, sizeof(Value));
	return Value;
}

void FVoxelLandPowerInternals::Decompose(uint32 Magnitude, double& Mantissa, int32& Exponent)
{
	const uint32 Fraction = Magnitude & 0x7fffffu;
	const uint32 EncodedExponent = Magnitude >> 23;
	if (EncodedExponent != 0)
	{
		Mantissa = 1.0 + double(Fraction) / 8388608.0;
		Exponent = int32(EncodedExponent) - 127;
		return;
	}
	int32 Highest = 0;
	uint32 Remaining = Fraction;
	while (Remaining > 1)
	{
		Remaining >>= 1;
		++Highest;
	}
	Mantissa = double(Fraction) / double(uint32(1) << Highest);
	Exponent = Highest - 149;
}

double FVoxelLandPowerInternals::Decode(uint32 Magnitude)
{
	if (Magnitude == 0)
	{
		return 0.0;
	}
	double Mantissa;
	int32 Exponent;
	Decompose(Magnitude, Mantissa, Exponent);
	return Mantissa * PowerOfTwo(Exponent);
}

double FVoxelLandPowerInternals::Log2Mantissa(double Mantissa)
{
	const double T = (Mantissa - 1.0) / (Mantissa + 1.0);
	const double T2 = T * T;
	double Term = T;
	double Sum = T;
	for (int32 Index = 1; Index < 20; ++Index)
	{
		Term *= T2;
		Sum += Term / double(2 * Index + 1);
	}
	return (2.0 * Sum) * 1.44269504088896340735992468100189214;
}

double FVoxelLandPowerInternals::Exp2Fraction(double Fraction)
{
	const double X = Fraction * 0.693147180559945309417232121458176568;
	double Sum = 1.0;
	double Term = 1.0;
	for (int32 Index = 1; Index <= 20; ++Index)
	{
		Term = (Term * X) / double(Index);
		Sum += Term;
	}
	return Sum;
}

uint32 FVoxelLandPowerInternals::RoundEven(double Value)
{
	const uint32 Integer = uint32(Value);
	const double Fraction = Value - double(Integer);
	return Integer + uint32(Fraction > 0.5 || (Fraction == 0.5 && (Integer & 1u) != 0));
}

bool FVoxelLandPowerInternals::PackResult(double Log2Result, bool bNegative, float& Result)
{
	uint32 Bits = bNegative ? 0x80000000u : 0u;
	if (Log2Result < -150.0)
	{
		FMemory::Memcpy(&Result, &Bits, sizeof(Result));
		return true;
	}
	if (Log2Result >= 128.0)
	{
		return false;
	}
	int32 Integral = int32(Log2Result);
	if (double(Integral) > Log2Result)
	{
		--Integral;
	}
	const double Significand = Exp2Fraction(Log2Result - double(Integral));
	if (Integral >= -126)
	{
		uint32 Rounded = RoundEven(Significand * 8388608.0);
		if (Rounded == 0x1000000u)
		{
			Rounded >>= 1;
			++Integral;
		}
		if (Integral >= 128)
		{
			return false;
		}
		Bits |= (uint32(Integral + 127) << 23) | (Rounded - 0x800000u);
	}
	else
	{
		const uint32 Rounded = RoundEven(Significand * PowerOfTwo(Integral + 149));
		Bits |= Rounded;
	}
	FMemory::Memcpy(&Result, &Bits, sizeof(Result));
	return true;
}

bool FVoxelLandPower::Evaluate(float Base, float Exponent, float& Result)
{
	uint32 BaseBits;
	uint32 ExponentBits;
	FMemory::Memcpy(&BaseBits, &Base, sizeof(BaseBits));
	FMemory::Memcpy(&ExponentBits, &Exponent, sizeof(ExponentBits));
	const uint32 BaseMagnitude = BaseBits & 0x7fffffffu;
	const uint32 ExponentMagnitude = ExponentBits & 0x7fffffffu;
	if (BaseMagnitude >= 0x7f800000u || ExponentMagnitude >= 0x7f800000u)
	{
		return false;
	}
	const bool bNegativeBase = (BaseBits & 0x80000000u) != 0 && BaseMagnitude != 0;
	const bool bNegativeExponent = (ExponentBits & 0x80000000u) != 0 && ExponentMagnitude != 0;
	const double ExponentValue = FVoxelLandPowerInternals::Decode(ExponentMagnitude) * (bNegativeExponent ? -1.0 : 1.0);
	bool bNegativeResult = false;
	if (bNegativeBase)
	{
		if (ExponentValue < -32.0 || ExponentValue > 32.0 || double(int32(ExponentValue)) != ExponentValue)
		{
			return false;
		}
		bNegativeResult = (int32(ExponentValue) & 1) != 0;
	}
	else if (bNegativeExponent || ExponentMagnitude == 0)
	{
		return false;
	}
	if (BaseMagnitude == 0)
	{
		Result = 0.f;
		return true;
	}
	double Mantissa;
	int32 BaseExponent;
	FVoxelLandPowerInternals::Decompose(BaseMagnitude, Mantissa, BaseExponent);
	const double Logarithm = double(BaseExponent) + FVoxelLandPowerInternals::Log2Mantissa(Mantissa);
	return FVoxelLandPowerInternals::PackResult(Logarithm * ExponentValue, bNegativeResult, Result);
}