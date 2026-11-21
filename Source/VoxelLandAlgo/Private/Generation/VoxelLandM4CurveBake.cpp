#include "Generation/VoxelLandM4CurveBake.h"

namespace VoxelLandM4CurveBake
{
bool Bake(TConstArrayView<FVoxelLandM4CurveKey> Keys, int32 Minimum, int32 Maximum, TArray<int32>& OutSamples,
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe>& OutCurve, FString& OutError)
{
	OutError.Reset();
	if (Minimum > Maximum || Keys.Num() < 2 || Keys.Num() > 65536)
	{
		OutError = TEXT("Invalid curve range or key count");
		return false;
	}
	if (Keys[0].InputQ16 != 0 || Keys[Keys.Num() - 1].InputQ16 != 65535)
	{
		OutError = TEXT("Curve must have keys at both normalized endpoints");
		return false;
	}
	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		if (Keys[Index].Output < Minimum || Keys[Index].Output > Maximum
			|| (Index > 0 && Keys[Index - 1].InputQ16 >= Keys[Index].InputQ16))
		{
			OutError = FString::Printf(TEXT("Curve key %d is out of range or not strictly ordered"), Index);
			return false;
		}
	}

	constexpr int64 SampleIntervals = FVoxelLandM4CurveLut::SampleCount - 1;
	TArray<int32> Samples;
	Samples.Reserve(FVoxelLandM4CurveLut::SampleCount);
	int32 Segment = 0;
	for (int32 Index = 0; Index < FVoxelLandM4CurveLut::SampleCount; ++Index)
	{
		const int64 Position = int64(Index) * 65535;
		while (Segment + 1 < Keys.Num() - 1 && Position > int64(Keys[Segment + 1].InputQ16) * SampleIntervals)
		{
			++Segment;
		}
		const FVoxelLandM4CurveKey& Left = Keys[Segment];
		const FVoxelLandM4CurveKey& Right = Keys[Segment + 1];
		const int64 LeftPosition = int64(Left.InputQ16) * SampleIntervals;
		const int64 Width = int64(Right.InputQ16 - Left.InputQ16) * SampleIntervals;
		const int64 RightWeight = Position - LeftPosition;
		const int64 LeftWeight = Width - RightWeight;
		// Nonnegative weights sum to at most 65535*4096, so even the full
		// int32 range produces an absolute sum below 2^60.
		const int64 Numerator = int64(Left.Output) * LeftWeight + int64(Right.Output) * RightWeight;
		int64 Rounded = Numerator / Width;
		const int64 Remainder = Numerator % Width;
		const int64 Magnitude = Remainder < 0 ? -Remainder : Remainder;
		if (Magnitude != 0 && Magnitude >= Width - Magnitude)
		{
			Rounded += Numerator < 0 ? -1 : 1;
		}
		Samples.Add(int32(Rounded));
	}

	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
	if (!FVoxelLandM4CurveLut::Create(Samples, Minimum, Maximum, Curve, OutError))
	{
		return false;
	}
	OutSamples = MoveTemp(Samples);
	OutCurve = MoveTemp(Curve);
	return true;
}
}
