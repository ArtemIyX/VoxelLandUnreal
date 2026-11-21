#if WITH_DEV_AUTOMATION_TESTS

#include "Generation/VoxelLandM4HeightGrid.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace
{
constexpr EAutomationTestFlags M4HeightGridFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4HeightGridSampleTest, "VoxelLand.M4.HeightGrid.Sample", M4HeightGridFlags)
bool FVoxelLandM4HeightGridSampleTest::RunTest(const FString& Parameters)
{
	TArray<int32> Source = {-100, 0, 100, 100, 200, 300};
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	if (!TestTrue(TEXT("grid creation succeeds"), FVoxelLandM4HeightGrid::Create(-15, -10, 2, 1, 10, Source, Grid)))
	{
		return false;
	}
	Source[0] = 999;
	int32 Height = 0;
	TestTrue(TEXT("negative origin is included"), Grid->Sample(-15, -10, Height));
	TestEqual(TEXT("snapshot owns original input"), Height, -100);
	TestTrue(TEXT("inclusive upper endpoint"), Grid->Sample(5, 0, Height));
	TestEqual(TEXT("upper endpoint selects last sample"), Height, 300);
	TestTrue(TEXT("interior negative position"), Grid->Sample(-10, -5, Height));
	TestEqual(TEXT("four-corner average"), Height, 50);
	TestTrue(TEXT("vertical internal boundary"), Grid->Sample(-5, -10, Height));
	TestEqual(TEXT("internal sample is exact"), Height, 0);
	Height = 42;
	TestFalse(TEXT("just outside left edge rejected"), Grid->Sample(-16, -10, Height));
	TestFalse(TEXT("just outside upper edge rejected"), Grid->Sample(5, 1, Height));
	TestEqual(TEXT("failed samples preserve output"), Height, 42);

	const TArray<int32> Half = {-1, 0, -1, 0};
	TestTrue(TEXT("signed tie grid created"), FVoxelLandM4HeightGrid::Create(-10, -10, 1, 1, 10, Half, Grid));
	TestTrue(TEXT("signed half sample"), Grid->Sample(-5, -10, Height));
	TestEqual(TEXT("negative half rounds away from zero"), Height, -1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4HeightGridValidationTest, "VoxelLand.M4.HeightGrid.Validation", M4HeightGridFlags)
bool FVoxelLandM4HeightGridValidationTest::RunTest(const FString& Parameters)
{
	const TArray<int32> Values = {0, 1, 2, 3};
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	if (!TestTrue(TEXT("baseline grid"), FVoxelLandM4HeightGrid::Create(0, 0, 1, 1, 10, Values, Grid)))
	{
		return false;
	}
	const FVoxelLandM4HeightGrid* const Previous = Grid.Get();
	for (const int32 CancelAt : {2, 5, 6})
	{
		int32 Checks = 0;
		TestFalse(TEXT("validation, copying and publication cancellation reject"),
			FVoxelLandM4HeightGrid::Create(
				0, 0, 1, 1, 10, Values, Grid, [&Checks, CancelAt] { return ++Checks >= CancelAt; }));
		TestTrue(TEXT("cancelled creation preserves prior snapshot"), Grid.Get() == Previous);
	}
	TestFalse(TEXT("origin endpoint overflow rejected"),
		FVoxelLandM4HeightGrid::Create(std::numeric_limits<int64>::max() - 5, 0, 1, 1, 10, Values, Grid));
	TestFalse(TEXT("zero spacing rejected"), FVoxelLandM4HeightGrid::Create(0, 0, 1, 1, 0, Values, Grid));
	TestFalse(TEXT("excess spacing rejected"),
		FVoxelLandM4HeightGrid::Create(0, 0, 1, 1, std::numeric_limits<int64>::max(), Values, Grid));
	TestFalse(TEXT("short sample array rejected"), FVoxelLandM4HeightGrid::Create(0, 0, 2, 1, 10, Values, Grid));
	TestFalse(TEXT("excess dimension rejected"), FVoxelLandM4HeightGrid::Create(0, 0, 1025, 1, 10, Values, Grid));
	const TArray<int32> Invalid = {0, 1, 2, 512000001};
	TestFalse(TEXT("out of range height rejected"), FVoxelLandM4HeightGrid::Create(0, 0, 1, 1, 10, Invalid, Grid));
	TestTrue(TEXT("failed creation preserves prior snapshot"), Grid.Get() == Previous);
	const int64 Minimum = std::numeric_limits<int64>::min();
	TestTrue(TEXT("minimum origin accepted when endpoint fits"),
		FVoxelLandM4HeightGrid::Create(Minimum, Minimum, 1, 1, 10, Values, Grid));
	int32 Height = -1;
	TestTrue(TEXT("minimum origin sampled"), Grid->Sample(Minimum, Minimum, Height));
	TestEqual(TEXT("minimum origin maps to first value"), Height, 0);
	return true;
}

#endif
