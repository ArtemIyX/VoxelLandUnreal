#if WITH_DEV_AUTOMATION_TESTS

#include "Generation/Terrain/Shaping/VoxelLandSlope.h"
#include "Misc/AutomationTest.h"
#include <cmath>
#include <limits>

namespace
{
constexpr EAutomationTestFlags M4SlopeTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4SlopeNumericTest, "VoxelLand.M4.Slope.Numeric", M4SlopeTestFlags)
bool FVoxelLandM4SlopeNumericTest::RunTest(const FString& Parameters)
{
	int64 Density = 0;
	TestTrue(TEXT("flat terrain evaluated"), VoxelLandM4Slope::DensityQ8(100 * 256, 0, 0, 0, 0, 0, 100, Density));
	TestEqual(TEXT("flat terrain equals Z minus height"), Density, int64(100 * 256));
	// A 3/4 slope has a 5/4 normalization factor, an exact Q12 norm.
	TestTrue(TEXT("three-quarter slope evaluated"),
		VoxelLandM4Slope::DensityQ8(125 * 256, 0, 75 * 256, -75 * 256, 0, 0, 100, Density));
	TestEqual(TEXT("3/4 slope has exact 5/4 norm"), Density, int64(100 * 256));
	int64 ReverseSlopeDensity = 0;
	TestTrue(TEXT("negative slope evaluated"),
		VoxelLandM4Slope::DensityQ8(125 * 256, 0, -75 * 256, 75 * 256, 0, 0, 100, ReverseSlopeDensity));
	TestEqual(TEXT("slope sign does not change norm"), ReverseSlopeDensity, Density);
	int64 NegativeDensity = 0;
	TestTrue(TEXT("negative heights evaluated"), VoxelLandM4Slope::DensityQ8(-225 * 256, -100 * 256, -25 * 256,
													 -175 * 256, -100 * 256, -100 * 256, 100, NegativeDensity));
	TestEqual(TEXT("signed distance is mirrored"), NegativeDensity, -Density);

	// Independent floating-point geometry comparison: Q12 normal rounding and
	// Q8 output rounding permit a small bounded error at these moderate slopes.
	const int32 Steps[][2] = {{0, 0}, {25, 75}, {-100, 50}, {200, -125}};
	for (const auto& Step : Steps)
	{
		const bool Ok = VoxelLandM4Slope::DensityQ8(
			100 * 256, 0, Step[0] * 256, -Step[0] * 256, Step[1] * 256, -Step[1] * 256, 100, Density);
		if (!TestTrue(TEXT("finite reference slope evaluated"), Ok))
		{
			continue;
		}
		const double X = double(Step[0]) / 100.0;
		const double Y = double(Step[1]) / 100.0;
		const double ReferenceQ8 = 100.0 * 256.0 / std::sqrt(1.0 + X * X + Y * Y);
		TestTrue(TEXT("density agrees with independent geometry reference within Q12 precision"),
			std::abs(double(Density) - ReferenceQ8) <= 8.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4SlopeInvalidTest, "VoxelLand.M4.Slope.Invalid", M4SlopeTestFlags)
bool FVoxelLandM4SlopeInvalidTest::RunTest(const FString& Parameters)
{
	int64 Density = 17;
	TestFalse(TEXT("zero voxel size rejected"), VoxelLandM4Slope::DensityQ8(0, 0, 0, 0, 0, 0, 0, Density));
	TestFalse(TEXT("oversize voxel rejected"), VoxelLandM4Slope::DensityQ8(0, 0, 0, 0, 0, 0, 2000001, Density));
	TestFalse(TEXT("out-of-range position rejected"),
		VoxelLandM4Slope::DensityQ8(std::numeric_limits<int64>::min(), 0, 0, 0, 0, 0, 1, Density));
	TestFalse(TEXT("out-of-range height rejected"), VoxelLandM4Slope::DensityQ8(0, 512000001, 0, 0, 0, 0, 1, Density));
	TestFalse(TEXT("slope outside representable range rejected"),
		VoxelLandM4Slope::DensityQ8(0, 0, 512000000, -512000000, 0, 0, 1, Density));
	TestEqual(TEXT("failure preserves prior output"), Density, int64(17));
	return true;
}

#endif
