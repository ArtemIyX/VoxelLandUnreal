#include "Generation/VoxelLandM4CurveLut.h"

bool FVoxelLandM4CurveLut::Create(TConstArrayView<int32> Samples, int32 Minimum, int32 Maximum,
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe>& OutCurve, FString& OutError)
{
	OutCurve.Reset();
	OutError.Reset();
	if (Minimum > Maximum)
	{
		OutError = TEXT("Curve output minimum exceeds maximum");
		return false;
	}
	if (Samples.Num() != SampleCount)
	{
		OutError = TEXT("Curve LUT must contain exactly 4097 samples");
		return false;
	}
	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		if (Samples[Index] < Minimum || Samples[Index] > Maximum)
		{
			OutError = FString::Printf(TEXT("Curve sample %d is outside its declared output range"), Index);
			return false;
		}
	}
	TSharedPtr<FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve = MakeShareable(new FVoxelLandM4CurveLut());
	Curve->Values.Append(Samples.GetData(), Samples.Num());
	OutCurve = MoveTemp(Curve);
	return true;
}

int32 FVoxelLandM4CurveLut::Evaluate(uint16 Input) const
{
	constexpr int64 Denominator = 65535;
	const int64 Position = int64(Input) * (SampleCount - 1);
	const int32 Index = int32(Position / Denominator);
	const int64 Remainder = Position % Denominator;
	if (Index == SampleCount - 1)
	{
		return Values[Index];
	}
	// Both weighted products and their sum fit int64 for the full int32 range:
	// weights sum to 65535, yielding magnitude at most 2^31 * 65535.
	const int64 Numerator = int64(Values[Index]) * (Denominator - Remainder) + int64(Values[Index + 1]) * Remainder;
	const int64 Rounded =
		Numerator >= 0 ? (Numerator + Denominator / 2) / Denominator : -((-Numerator + Denominator / 2) / Denominator);
	return int32(Rounded);
}