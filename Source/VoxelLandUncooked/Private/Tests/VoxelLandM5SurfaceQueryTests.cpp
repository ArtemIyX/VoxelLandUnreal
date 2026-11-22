#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5SurfaceQuery.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5SurfaceQueryFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5FlatSurfaceQueryTest, "VoxelLand.M5.SurfaceQuery.FlatAndFailures", M5SurfaceQueryFlags)
bool FVoxelLandM5FlatSurfaceQueryTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::SurfaceQuery;
	const FIntVector Dims(9, 9, 4);
	FVoxelLandM5PointCm Origin;
	Origin.X = -400;
	Origin.Y = -400;
	Origin.Z = -200;
	TArray<int16> Samples;
	Samples.Init(-10, 9 * 9 * 4);
	for (int32 Index = 9 * 9 * 2; Index < Samples.Num(); ++Index)
	{
		Samples[Index] = 10;
	}
	TArray<uint8> Protection;
	Protection.Init(0, 9 * 9);
	int64 Height = 123, MinHeight = 124, MaxHeight = 125;
	int32 Slope = 126;
	bool bProtected = true, bFlooded = false;
	const auto NeverCancel = []() {
		return false;
	};
	const auto Query = [&](int64 X, int64 Y, int32 Radius, int64 Budget, TFunctionRef<bool()> IsCancelled) {
		return QueryFlatSurface(Dims, Origin, 100, Samples, Protection, X, Y, Radius, true, -50, Budget, IsCancelled,
			Height, MinHeight, MaxHeight, Slope, bProtected, bFlooded);
	};
	auto Result = Query(-100, -100, 0, 144, NeverCancel);
	TestEqual(TEXT("flat midpoint accepted"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::None));
	TestEqual(TEXT("height is exact negative midpoint"), Height, int64(-50));
	TestEqual(TEXT("minimum height"), MinHeight, int64(-50));
	TestEqual(TEXT("maximum height"), MaxHeight, int64(-50));
	TestEqual(TEXT("flat slope"), Slope, 0);
	TestFalse(TEXT("unprotected footprint"), bProtected);
	TestTrue(TEXT("water at surface floods"), bFlooded);
	Protection[1 + 9] = 1;
	Result = Query(-100, -100, 0, 144, NeverCancel);
	TestEqual(TEXT("halo protection valid"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::None));
	TestTrue(TEXT("halo protection conservatively propagated"), bProtected);
	Protection[1 + 9] = 0;

	Height = 123;
	Samples[3 + 9 * 3 + 81 * 3] = -2; // A second crossing in a single column.
	Result = Query(-100, -100, 0, 144, NeverCancel);
	TestEqual(TEXT("overhang rejected"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::Unproven));
	TestEqual(TEXT("failure leaves height"), Height, int64(123));
	Samples[3 + 9 * 3 + 81 * 3] = 10;
	Samples[3 + 9 * 3 + 81 * 2] = 0;
	Result = Query(-100, -100, 0, 144, NeverCancel);
	TestEqual(TEXT("zero boundary rejected"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::Unproven));
	Samples[3 + 9 * 3 + 81 * 2] = 9;
	Result = Query(-100, -100, 0, 144, NeverCancel);
	TestEqual(TEXT("varying crossing rejected"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::Unproven));
	Samples[3 + 9 * 3 + 81 * 2] = 10;
	Result = Query(-400, -100, 0, 144, NeverCancel);
	TestEqual(TEXT("missing halo rejected"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::OutOfDomain));
	Result = Query(-100, -100, 0, 143, NeverCancel);
	TestEqual(TEXT("work cap rejected"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::BudgetExceeded));
	int32 Calls = 0;
	const auto CancelLater = [&Calls]() {
		return ++Calls == 2;
	};
	Result = Query(-100, -100, 0, 144, CancelLater);
	TestEqual(TEXT("mid scan cancellation"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::Cancelled));
	TestEqual(TEXT("cancel leaves height"), Height, int64(123));
	return true;
}
#endif
