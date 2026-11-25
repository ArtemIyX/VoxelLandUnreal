#if WITH_DEV_AUTOMATION_TESTS

#include "Generation/Terrain/Shaping/VoxelLandOverhang.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace
{
constexpr EAutomationTestFlags M4OverhangFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4OverhangKernelTest, "VoxelLand.M4.Overhang.Kernel", M4OverhangFlags)

bool FVoxelLandM4OverhangKernelTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4OverhangSettings Settings;
	Settings.bEnabled = true;
	Settings.AmplitudeQ8 = 1;
	Settings.BandHalfWidthQ8 = 2;
	int32 Delta = 91;
	TestTrue(TEXT("positive tie succeeds"), VoxelLandM4Overhang::DisplacementQ8(Settings, 1, 32767, Delta));
	TestEqual(TEXT("positive half rounds away"), Delta, 1);
	TestTrue(TEXT("negative tie succeeds"), VoxelLandM4Overhang::DisplacementQ8(Settings, -1, -32767, Delta));
	TestEqual(TEXT("negative half rounds away"), Delta, -1);
	TestTrue(TEXT("upper band endpoint"), VoxelLandM4Overhang::DisplacementQ8(Settings, 2, 32767, Delta));
	TestEqual(TEXT("endpoint displacement is zero"), Delta, 0);
	TestTrue(TEXT("minimum height safe"),
		VoxelLandM4Overhang::DisplacementQ8(Settings, std::numeric_limits<int64>::min(), 32767, Delta));
	TestEqual(TEXT("outside displacement is zero"), Delta, 0);
	Settings.bEnabled = false;
	TestTrue(TEXT("disabled succeeds"), VoxelLandM4Overhang::DisplacementQ8(Settings, 0, 32767, Delta));
	TestEqual(TEXT("disabled is identity"), Delta, 0);
	Settings.bEnabled = true;
	Settings.AmplitudeQ8 = 0;
	TestTrue(TEXT("zero amplitude succeeds"), VoxelLandM4Overhang::DisplacementQ8(Settings, 0, 32767, Delta));
	TestEqual(TEXT("zero amplitude is identity"), Delta, 0);
	Settings.AmplitudeQ8 = 3;
	Delta = 91;
	TestFalse(TEXT("amplitude above band rejected"), VoxelLandM4Overhang::DisplacementQ8(Settings, 0, 0, Delta));
	TestEqual(TEXT("failure does not alter output"), Delta, 91);
	Settings.AmplitudeQ8 = 1;
	TestFalse(TEXT("invalid quantized noise rejected"), VoxelLandM4Overhang::DisplacementQ8(Settings, 0, 32768, Delta));
	TestEqual(TEXT("invalid noise leaves output"), Delta, 91);
	int32 Noise = 5;
	TestFalse(TEXT("nonfinite noise rejected"),
		VoxelLandM4Overhang::QuantizeNoise(std::numeric_limits<float>::infinity(), Noise));
	TestEqual(TEXT("nonfinite leaves output"), Noise, 5);
	TestTrue(TEXT("positive noise clamped"), VoxelLandM4Overhang::QuantizeNoise(2.f, Noise));
	TestEqual(TEXT("positive maximum"), Noise, 32767);
	TestTrue(TEXT("negative noise clamped"), VoxelLandM4Overhang::QuantizeNoise(-2.f, Noise));
	TestEqual(TEXT("negative maximum"), Noise, -32767);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4OverhangCrossingsTest, "VoxelLand.M4.Overhang.AnalyticCrossings", M4OverhangFlags)

bool FVoxelLandM4OverhangCrossingsTest::RunTest(const FString& Parameters)
{
	// Independent analytic field n(x,y,z) = -sin(pi * (x+y+z) / 50 cm).
	// At x=y=0 these four positions are exact extrema. Its continuous density
	// h + displacement has opposite signs at each successive pair, proving
	// at least three zero crossings by the intermediate value theorem.
	FVoxelLandM4OverhangSettings Settings;
	Settings.bEnabled = true;
	Settings.AmplitudeQ8 = 100 * 256;
	Settings.BandHalfWidthQ8 = 400 * 256;
	const int32 Heights[] = {-75 * 256, -25 * 256, 25 * 256, 75 * 256};
	const int32 Noises[] = {-32767, 32767, -32767, 32767};
	int32 Previous = 0;
	int32 Crossings = 0;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Heights); ++Index)
	{
		int32 Delta = 0;
		if (!TestTrue(TEXT("analytic sample succeeds"),
				VoxelLandM4Overhang::DisplacementQ8(Settings, Heights[Index], Noises[Index], Delta)))
		{
			return false;
		}
		const int32 Density = Heights[Index] + Delta;
		if (Index > 0 && (Density < 0) != (Previous < 0))
		{
			++Crossings;
		}
		Previous = Density;
	}
	TestEqual(TEXT("Z-dependent displacement creates three crossings"), Crossings, 3);
	return true;
}

#endif
