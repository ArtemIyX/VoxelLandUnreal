#if WITH_DEV_AUTOMATION_TESTS

#include "Generation/Terrain/Curves/VoxelLandCurveLut.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace
{
constexpr EAutomationTestFlags M4CurveFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4CurveInterpolation, "VoxelLand.M4.Curve.Interpolation", M4CurveFlags)
bool FVoxelLandM4CurveInterpolation::RunTest(const FString& Parameters)
{
	TArray<int32> Samples;
	Samples.Reserve(FVoxelLandM4CurveLut::SampleCount);
	for (int32 Index = 0; Index < FVoxelLandM4CurveLut::SampleCount; ++Index)
	{
		Samples.Add(Index - 2048);
	}
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
	FString Error;
	if (!TestTrue(TEXT("valid curve creates immutable evaluator"),
			FVoxelLandM4CurveLut::Create(Samples, -2048, 2048, Curve, Error)))
	{
		AddError(Error);
		return false;
	}
	// An independently simplified reference for a linear LUT: the exact result
	// is Input * 4096 / 65535 - 2048, rounded once away from zero.
	for (int32 Input = 0; Input <= 65535; ++Input)
	{
		const int64 Numerator = int64(Input) * 4096 - int64(2048) * 65535;
		const int32 Expected = int32(Numerator >= 0 ? (Numerator + 32767) / 65535 : -((-Numerator + 32767) / 65535));
		if (Curve->Evaluate(uint16(Input)) != Expected)
		{
			AddError(FString::Printf(TEXT("linear curve mismatch at input %d"), Input));
			break;
		}
	}
	Samples[0] = 99;
	TestEqual(TEXT("curve owns a snapshot"), Curve->Evaluate(0), -2048);
	TestEqual(TEXT("last endpoint exact"), Curve->Evaluate(65535), 2048);

	Samples.Init(0, FVoxelLandM4CurveLut::SampleCount);
	Samples[1] = 1;
	TestTrue(TEXT("positive transition accepted"), FVoxelLandM4CurveLut::Create(Samples, -1, 1, Curve, Error));
	if (Curve)
	{
		TestEqual(TEXT("below halfway rounds down"), Curve->Evaluate(7), 0);
		TestEqual(TEXT("above halfway rounds up"), Curve->Evaluate(8), 1);
	}
	Samples[1] = -1;
	TestTrue(TEXT("negative transition accepted"), FVoxelLandM4CurveLut::Create(Samples, -1, 1, Curve, Error));
	if (Curve)
	{
		TestEqual(TEXT("negative below halfway rounds to zero"), Curve->Evaluate(7), 0);
		TestEqual(TEXT("negative above halfway rounds away"), Curve->Evaluate(8), -1);
	}
	Samples.Init(std::numeric_limits<int32>::min(), FVoxelLandM4CurveLut::SampleCount);
	Samples[1] = std::numeric_limits<int32>::max();
	TestTrue(
		TEXT("full integer range accepted"), FVoxelLandM4CurveLut::Create(Samples, std::numeric_limits<int32>::min(),
												 std::numeric_limits<int32>::max(), Curve, Error));
	if (Curve)
	{
		TestEqual(TEXT("extreme endpoint exact"), Curve->Evaluate(0), std::numeric_limits<int32>::min());
		// input 8 has remainder 32768/65535: interpolate the two extremes.
		TestEqual(TEXT("extreme interpolation uses wide intermediates"), Curve->Evaluate(8), 32768);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4CurveValidation, "VoxelLand.M4.Curve.Validation", M4CurveFlags)
bool FVoxelLandM4CurveValidation::RunTest(const FString& Parameters)
{
	TArray<int32> Samples;
	Samples.Init(0, FVoxelLandM4CurveLut::SampleCount);
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
	FString Error;
	TestTrue(TEXT("baseline valid"), FVoxelLandM4CurveLut::Create(Samples, 0, 1, Curve, Error));
	Samples.Pop();
	TestFalse(TEXT("short LUT rejected"), FVoxelLandM4CurveLut::Create(Samples, 0, 1, Curve, Error));
	TestFalse(TEXT("failure clears old curve"), Curve.IsValid());
	TestFalse(TEXT("size error explains failure"), Error.IsEmpty());
	Samples.Add(2);
	TestFalse(TEXT("out of range sample rejected"), FVoxelLandM4CurveLut::Create(Samples, 0, 1, Curve, Error));
	TestFalse(TEXT("inverted bounds rejected"), FVoxelLandM4CurveLut::Create(Samples, 1, 0, Curve, Error));
	Samples.Add(0);
	TestFalse(TEXT("long LUT rejected"), FVoxelLandM4CurveLut::Create(Samples, 0, 2, Curve, Error));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
