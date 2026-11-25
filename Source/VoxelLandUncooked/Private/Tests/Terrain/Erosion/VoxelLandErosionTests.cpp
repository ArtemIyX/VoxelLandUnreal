#if WITH_DEV_AUTOMATION_TESTS

#include "Generation/Terrain/Erosion/VoxelLandErosion.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M4ErosionFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
int64 SumM4ErosionHeights(const TArray<int32>& Values)
{
	int64 Result = 0;
	for (int32 Value : Values)
	{
		Result += Value;
	}
	return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4ErosionHydraulicTest, "VoxelLand.M4.Erosion.Hydraulic", M4ErosionFlags)
bool FVoxelLandM4ErosionHydraulicTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4ErosionSettings Settings;
	Settings.ThermalIterations = 0;
	Settings.MaxSteps = 3;
	const TArray<int32> Input = {100, 60, 59, 100, 100, 100};
	const TArray<FIntPoint> Starts = {FIntPoint(0, 0)};
	TArray<int32> Output;
	const FVoxelLandError Error =
		VoxelLandM4Erosion::Apply(2, 1, Input, Starts, Settings, Output, [] { return false; });
	TestFalse(TEXT("hydraulic succeeds"), bool(Error));
	if (Output.Num() != 6)
	{
		AddError(TEXT("unexpected output size"));
		return false;
	}
	TestEqual(TEXT("first cell erodes"), Output[0], 95);
	TestEqual(TEXT("intermediate capacity drop deposits"), Output[1], 62);
	TestEqual(TEXT("local minimum receives remaining sediment"), Output[2], 62);
	TestEqual(TEXT("mass conserved"), SumM4ErosionHeights(Output), SumM4ErosionHeights(Input));

	Settings.MaxSteps = 1;
	TArray<int32> StepCap;
	const FVoxelLandError StepError =
		VoxelLandM4Erosion::Apply(2, 1, Input, Starts, Settings, StepCap, [] { return false; });
	TestFalse(TEXT("step-limited hydraulic succeeds"), bool(StepError));
	if (StepCap.Num() == 6)
	{
		TestEqual(TEXT("step cap deposits carried sediment"), StepCap[1], 65);
		TestEqual(TEXT("step cap conserves mass"), SumM4ErosionHeights(StepCap), SumM4ErosionHeights(Input));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4ErosionThermalTest, "VoxelLand.M4.Erosion.Thermal", M4ErosionFlags)
bool FVoxelLandM4ErosionThermalTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4ErosionSettings Settings;
	Settings.ThermalIterations = 1;
	Settings.TalusQ8 = 0;
	const TArray<int32> Input = {100, 0, 0, 0};
	const TArray<FIntPoint> Starts;
	TArray<int32> Output;
	const FVoxelLandError Error =
		VoxelLandM4Erosion::Apply(1, 1, Input, Starts, Settings, Output, [] { return false; });
	TestFalse(TEXT("thermal succeeds"), bool(Error));
	if (Output.Num() != 4)
	{
		AddError(TEXT("unexpected output size"));
		return false;
	}
	TestEqual(TEXT("source loses a quarter"), Output[0], 75);
	TestEqual(TEXT("+X wins equal-height neighbor tie"), Output[1], 25);
	TestEqual(TEXT("+Y tie target is unchanged"), Output[2], 0);
	TestEqual(TEXT("thermal conserves mass"), SumM4ErosionHeights(Output), SumM4ErosionHeights(Input));
	TArray<int32> Repeat;
	const FVoxelLandError RepeatError =
		VoxelLandM4Erosion::Apply(1, 1, Input, Starts, Settings, Repeat, [] { return false; });
	TestFalse(TEXT("repeat succeeds"), bool(RepeatError));
	TestTrue(TEXT("repeat is identical"), Repeat == Output);
	const TArray<int32> NegativeInput = {-100, -200, -200, -200};
	TArray<int32> NegativeOutput;
	const FVoxelLandError NegativeError =
		VoxelLandM4Erosion::Apply(1, 1, NegativeInput, Starts, Settings, NegativeOutput, [] { return false; });
	TestFalse(TEXT("negative heights succeed"), bool(NegativeError));
	if (NegativeOutput.Num() == 4)
	{
		TestEqual(TEXT("negative source transfers"), NegativeOutput[0], -125);
		TestEqual(TEXT("negative target receives"), NegativeOutput[1], -175);
		TestEqual(TEXT("negative heights conserve mass"), SumM4ErosionHeights(NegativeOutput),
			SumM4ErosionHeights(NegativeInput));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4ErosionFailureTest, "VoxelLand.M4.Erosion.Failures", M4ErosionFlags)
bool FVoxelLandM4ErosionFailureTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4ErosionSettings Settings;
	const TArray<int32> Input = {-100, 0, 0, 100};
	const TArray<FIntPoint> Starts = {FIntPoint(1, 1)};
	TArray<int32> Output = {17};
	FVoxelLandError Error = VoxelLandM4Erosion::Apply(1, 1, Input, Starts, Settings, Output, [] { return true; });
	TestTrue(TEXT("cancelled"), Error.Code == EVoxelLandErrorCode::Cancelled);
	TestTrue(TEXT("cancel leaves output untouched"), Output == TArray<int32>({17}));
	int32 CancellationChecks = 0;
	Error = VoxelLandM4Erosion::Apply(
		1, 1, Input, Starts, Settings, Output, [&CancellationChecks] { return ++CancellationChecks >= 7; });
	TestTrue(TEXT("in-flight cancellation is reported"), Error.Code == EVoxelLandErrorCode::Cancelled);
	TestTrue(TEXT("in-flight cancellation does not publish"), Output == TArray<int32>({17}));
	const TArray<FIntPoint> NoStarts;
	CancellationChecks = 0;
	Error = VoxelLandM4Erosion::Apply(
		1, 1, Input, NoStarts, Settings, Output, [&CancellationChecks] { return ++CancellationChecks >= 5; });
	TestTrue(TEXT("thermal cancellation is reported"), Error.Code == EVoxelLandErrorCode::Cancelled);
	TestTrue(TEXT("thermal cancellation does not publish"), Output == TArray<int32>({17}));
	const TArray<FIntPoint> InvalidStarts = {FIntPoint(2, 1)};
	Error = VoxelLandM4Erosion::Apply(1, 1, Input, InvalidStarts, Settings, Output, [] { return false; });
	TestTrue(TEXT("out of bounds start rejected"), bool(Error));
	TestTrue(TEXT("error leaves output untouched"), Output == TArray<int32>({17}));
	Settings.ThermalIterations = 33;
	Error = VoxelLandM4Erosion::Apply(1, 1, Input, Starts, Settings, Output, [] { return false; });
	TestTrue(TEXT("work cap rejected"), bool(Error));
	Settings.ThermalIterations = 0;
	const TArray<int32> InvalidHeight = {512000001, 0, 0, 0};
	Error = VoxelLandM4Erosion::Apply(1, 1, InvalidHeight, Starts, Settings, Output, [] { return false; });
	TestTrue(TEXT("out-of-range height rejected"), bool(Error));
	TestTrue(TEXT("range failure does not publish"), Output == TArray<int32>({17}));
	return true;
}

#endif
