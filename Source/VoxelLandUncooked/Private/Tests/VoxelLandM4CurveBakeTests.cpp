#if WITH_DEV_AUTOMATION_TESTS

#include "Generation/VoxelLandM4CurveBake.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace
{
constexpr EAutomationTestFlags M4CurveBakeFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4CurveBakeLinearTest, "VoxelLand.M4.CurveBake.Linear", M4CurveBakeFlags)
bool FVoxelLandM4CurveBakeLinearTest::RunTest(const FString& Parameters)
{
	const TArray<FVoxelLandM4CurveKey> Keys = {{0, -2048}, {65535, 2048}};
	TArray<int32> Samples;
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
	FString Error;
	if (!TestTrue(TEXT("linear curve bakes"), VoxelLandM4CurveBake::Bake(Keys, -2048, 2048, Samples, Curve, Error)))
	{
		AddError(Error);
		return false;
	}
	if (!TestEqual(TEXT("sample count"), Samples.Num(), FVoxelLandM4CurveLut::SampleCount) || !Curve.IsValid())
	{
		return false;
	}
	// Independent closed form: the endpoints differ by 4096, so this curve
	// equals i - 2048 at every one of the 4097 exact bake positions.
	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		if (Samples[Index] != Index - 2048)
		{
			AddError(FString::Printf(TEXT("linear bake mismatch at index %d"), Index));
			break;
		}
	}
	TestEqual(TEXT("first input evaluates to first key"), Curve->Evaluate(0), -2048);
	TestEqual(TEXT("last input evaluates to last key"), Curve->Evaluate(65535), 2048);
	Samples[0] = 123;
	TestEqual(TEXT("published LUT owns its own sample snapshot"), Curve->Evaluate(0), -2048);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4CurveBakeRoundingTest, "VoxelLand.M4.CurveBake.Rounding", M4CurveBakeFlags)
bool FVoxelLandM4CurveBakeRoundingTest::RunTest(const FString& Parameters)
{
	// The middle bake position is 32767.5 in normalized uint16 key units.
	// Its value lies exactly halfway between the two adjacent keys.
	TArray<FVoxelLandM4CurveKey> Keys = {{0, 0}, {32767, 0}, {32768, 1}, {65535, 1}};
	TArray<int32> Samples;
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
	FString Error;
	if (!TestTrue(TEXT("positive halfway curve bakes"), VoxelLandM4CurveBake::Bake(Keys, -1, 1, Samples, Curve, Error)))
	{
		return false;
	}
	TestEqual(TEXT("positive tie rounds away"), Samples[2048], 1);
	Keys = {{0, -1}, {32767, -1}, {32768, 0}, {65535, 0}};
	if (!TestTrue(TEXT("negative halfway curve bakes"), VoxelLandM4CurveBake::Bake(Keys, -1, 1, Samples, Curve, Error)))
	{
		return false;
	}
	TestEqual(TEXT("negative tie rounds away"), Samples[2048], -1);
	Keys = {{0, std::numeric_limits<int32>::min()}, {65535, std::numeric_limits<int32>::max()}};
	if (!TestTrue(TEXT("full integer range bakes"), VoxelLandM4CurveBake::Bake(Keys, std::numeric_limits<int32>::min(),
														std::numeric_limits<int32>::max(), Samples, Curve, Error)))
	{
		return false;
	}
	TestEqual(TEXT("extreme negative endpoint"), Samples[0], std::numeric_limits<int32>::min());
	TestEqual(TEXT("extreme midpoint rounds away"), Samples[2048], -1);
	TestEqual(TEXT("extreme positive endpoint"), Samples[4096], std::numeric_limits<int32>::max());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4CurveBakeValidationTest, "VoxelLand.M4.CurveBake.Validation", M4CurveBakeFlags)
bool FVoxelLandM4CurveBakeValidationTest::RunTest(const FString& Parameters)
{
	TArray<FVoxelLandM4CurveKey> Keys = {{0, 0}, {65535, 1}};
	TArray<int32> Samples;
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
	FString Error;
	if (!TestTrue(TEXT("valid baseline"), VoxelLandM4CurveBake::Bake(Keys, 0, 1, Samples, Curve, Error)))
	{
		return false;
	}
	const TArray<int32> Original = Samples;
	const TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> OriginalCurve = Curve;
	Keys = {{0, 0}, {0, 1}, {65535, 1}};
	TestFalse(TEXT("duplicate position rejected"), VoxelLandM4CurveBake::Bake(Keys, 0, 1, Samples, Curve, Error));
	TestFalse(TEXT("failure provides diagnostic"), Error.IsEmpty());
	TestTrue(TEXT("failure leaves published samples unchanged"), Samples == Original);
	TestTrue(TEXT("failure leaves published LUT unchanged"), Curve == OriginalCurve);
	Keys = {{0, 0}, {40000, 1}, {30000, 0}, {65535, 1}};
	TestFalse(TEXT("unordered keys rejected"), VoxelLandM4CurveBake::Bake(Keys, 0, 1, Samples, Curve, Error));
	Keys = {{1, 0}, {65535, 1}};
	TestFalse(TEXT("missing first endpoint rejected"), VoxelLandM4CurveBake::Bake(Keys, 0, 1, Samples, Curve, Error));
	Keys = {{0, 0}, {65534, 1}};
	TestFalse(TEXT("missing last endpoint rejected"), VoxelLandM4CurveBake::Bake(Keys, 0, 1, Samples, Curve, Error));
	Keys = {{0, 0}, {65535, 2}};
	TestFalse(TEXT("out of range value rejected"), VoxelLandM4CurveBake::Bake(Keys, 0, 1, Samples, Curve, Error));
	TestFalse(TEXT("reversed output range rejected"), VoxelLandM4CurveBake::Bake(Keys, 1, 0, Samples, Curve, Error));
	Keys.Reset();
	TestFalse(TEXT("empty key list rejected"), VoxelLandM4CurveBake::Bake(Keys, 0, 1, Samples, Curve, Error));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
