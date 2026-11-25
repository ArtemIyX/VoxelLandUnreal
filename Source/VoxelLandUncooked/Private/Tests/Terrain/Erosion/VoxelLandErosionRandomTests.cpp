#if WITH_DEV_AUTOMATION_TESTS

#include "Generation/Terrain/Erosion/VoxelLandErosionRandom.h"
#include "Generation/Terrain/Erosion/VoxelLandErosion.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M4ErosionRandomFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4ErosionRandomVectors, "VoxelLand.M4.ErosionRandom.Vectors", M4ErosionRandomFlags)
bool FVoxelLandM4ErosionRandomVectors::RunTest(const FString& Parameters)
{
	// Fixed vectors calculated independently using unsigned 64-bit arithmetic.
	const FGuid StageId(0x12345678, 0x9abcdef0, 0x00fedcba, 0x98765432);
	constexpr uint64 WorldSeed = 123456789;
	constexpr uint32 Salt = 42;
	const uint32 Indices[] = {0, 1, 2, 3, 17, 65535};
	const FIntPoint Expected[] = {{22, 446}, {239, 108}, {184, 896}, {146, 894}, {42, 613}, {155, 664}};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Indices); ++Index)
	{
		FIntPoint Result(-1, -1);
		const FVoxelLandError Error = VoxelLandM4ErosionRandom::Start(
			WorldSeed, StageId, Salt, Indices[Index], 256, 1024, Result, [] { return false; });
		TestFalse(TEXT("fixed vector succeeds"), bool(Error));
		TestEqual(TEXT("fixed vector X"), Result.X, Expected[Index].X);
		TestEqual(TEXT("fixed vector Y"), Result.Y, Expected[Index].Y);
	}
	TArray<FIntPoint> Sequence;
	const FVoxelLandError Error =
		VoxelLandM4ErosionRandom::Starts(WorldSeed, StageId, Salt, 18, 256, 1024, Sequence, [] { return false; });
	TestFalse(TEXT("batch succeeds"), bool(Error));
	if (Sequence.Num() == 18)
	{
		TestEqual(TEXT("batch index 0 matches independent start"), Sequence[0], Expected[0]);
		TestEqual(TEXT("batch index 17 matches independent start"), Sequence[17], Expected[4]);
	}
	else
	{
		AddError(TEXT("batch count incorrect"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4ErosionRandomValidation, "VoxelLand.M4.ErosionRandom.Validation", M4ErosionRandomFlags)
bool FVoxelLandM4ErosionRandomValidation::RunTest(const FString& Parameters)
{
	const FGuid StageId(1, 2, 3, 4);
	FIntPoint Point(71, 72);
	FVoxelLandError Error = VoxelLandM4ErosionRandom::Start(1, StageId, 0, 0, 0, 1, Point, [] { return false; });
	TestTrue(TEXT("zero cells rejected"), bool(Error));
	TestEqual(TEXT("invalid request leaves point"), Point, FIntPoint(71, 72));
	Error = VoxelLandM4ErosionRandom::Start(1, FGuid(), 0, 0, 1, 1, Point, [] { return false; });
	TestTrue(TEXT("invalid stage id rejected"), bool(Error));
	Error = VoxelLandM4ErosionRandom::Start(1, StageId, 0, 65536, 1, 1, Point, [] { return false; });
	TestTrue(TEXT("droplet cap enforced"), bool(Error));
	int32 Checks = 0;
	Error = VoxelLandM4ErosionRandom::Start(1, StageId, 0, 0, 1, 1, Point, [&Checks] { return ++Checks == 2; });
	TestTrue(TEXT("cancellation between X and Y reported"), Error.Code == EVoxelLandErrorCode::Cancelled);
	TestEqual(TEXT("cancellation leaves point"), Point, FIntPoint(71, 72));
	TArray<FIntPoint> Points = {FIntPoint(7, 8)};
	Error = VoxelLandM4ErosionRandom::Starts(1, StageId, 0, 65537, 1, 1, Points, [] { return false; });
	TestTrue(TEXT("batch cap enforced"), bool(Error));
	TestTrue(TEXT("error leaves batch untouched"), Points.Num() == 1 && Points[0] == FIntPoint(7, 8));
	Checks = 0;
	Error = VoxelLandM4ErosionRandom::Starts(1, StageId, 0, 10, 1, 1, Points, [&Checks] { return ++Checks == 6; });
	TestTrue(TEXT("batch cancellation reported"), Error.Code == EVoxelLandErrorCode::Cancelled);
	TestTrue(TEXT("cancellation leaves batch untouched"), Points.Num() == 1 && Points[0] == FIntPoint(7, 8));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4ErosionRandomIntegration, "VoxelLand.M4.ErosionRandom.Integration", M4ErosionRandomFlags)
bool FVoxelLandM4ErosionRandomIntegration::RunTest(const FString& Parameters)
{
	const FGuid StageId(1, 2, 3, 4);
	TArray<FIntPoint> Starts;
	FVoxelLandError Error = VoxelLandM4ErosionRandom::Starts(42, StageId, 1, 32, 1, 1, Starts, [] { return false; });
	if (!TestFalse(TEXT("start generation succeeds"), bool(Error)))
	{
		return false;
	}
	bool bSawX0 = false, bSawX1 = false, bSawY0 = false, bSawY1 = false;
	for (const FIntPoint& Point : Starts)
	{
		bSawX0 |= Point.X == 0;
		bSawX1 |= Point.X == 1;
		bSawY0 |= Point.Y == 0;
		bSawY1 |= Point.Y == 1;
	}
	TestTrue(TEXT("both X endpoints appear"), bSawX0 && bSawX1);
	TestTrue(TEXT("both Y endpoints appear"), bSawY0 && bSawY1);
	FVoxelLandM4ErosionSettings Settings;
	Settings.ThermalIterations = 0;
	const TArray<int32> Input = {100, 50, 25, 0};
	TArray<int32> First, Second;
	Error = VoxelLandM4Erosion::Apply(1, 1, Input, Starts, Settings, First, [] { return false; });
	TestFalse(TEXT("erosion accepts generated starts"), bool(Error));
	Error = VoxelLandM4Erosion::Apply(1, 1, Input, Starts, Settings, Second, [] { return false; });
	TestFalse(TEXT("repeat erosion succeeds"), bool(Error));
	TestTrue(TEXT("repeat result agrees"), First.Num() == 4 && First == Second);
	return true;
}
#endif
