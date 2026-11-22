#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5FloorSupport.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5FloorSupportFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandM5PointCm FloorPoint(int64 X, int64 Y, int64 Z = 0)
{
	FVoxelLandM5PointCm Result;
	Result.X = X;
	Result.Y = Y;
	Result.Z = Z;
	return Result;
}

FVoxelLandM5ClosedBoxCm FloorRect(int64 MinX, int64 MinY, int64 MaxX, int64 MaxY)
{
	FVoxelLandM5ClosedBoxCm Result;
	Result.Min = FloorPoint(MinX, MinY);
	Result.Max = FloorPoint(MaxX, MaxY);
	return Result;
}

void AddFloorTile(TArray<FVoxelLandM5FloorTriangleCm>& Triangles, int64 MinX, int64 MinY, int64 MaxX, int64 MaxY)
{
	FVoxelLandM5FloorTriangleCm First;
	First.A = FloorPoint(MinX, MinY);
	First.B = FloorPoint(MaxX, MinY);
	First.C = FloorPoint(MaxX, MaxY);
	FVoxelLandM5FloorTriangleCm Second;
	Second.A = FloorPoint(MinX, MinY);
	Second.B = FloorPoint(MaxX, MaxY);
	Second.C = FloorPoint(MinX, MaxY);
	Triangles.Add(First);
	Triangles.Add(Second);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5FloorCoverageTest, "VoxelLand.M5.FloorSupport.Coverage", M5FloorSupportFlags)
bool FVoxelLandM5FloorCoverageTest::RunTest(const FString& Parameters)
{
	using VoxelLand::M5::FloorSupport::CheckHorizontalRectangularSupport;
	const auto NeverCancel = []() {
		return false;
	};
	const auto Footprint = FloorRect(0, 0, 20, 20);
	TArray<FVoxelLandM5FloorTriangleCm> Triangles;
	AddFloorTile(Triangles, 0, 0, 20, 20);
	const auto Check = [&](const FVoxelLandM5ClosedBoxCm& Box) {
		return CheckHorizontalRectangularSupport(Box, Triangles, 10000, NeverCancel).Code;
	};
	TestEqual(TEXT("two triangles exactly cover border"), int32(Check(Footprint)), int32(EVoxelLandM5ErrorCode::None));
	Triangles.RemoveAt(1);
	TestEqual(
		TEXT("one triangle cannot cover rectangle"), int32(Check(Footprint)), int32(EVoxelLandM5ErrorCode::Unproven));
	Triangles.Reset();
	AddFloorTile(Triangles, 0, 0, 9, 20);
	AddFloorTile(Triangles, 11, 0, 20, 20);
	TestEqual(TEXT("gap between covered endpoints is rejected"), int32(Check(Footprint)),
		int32(EVoxelLandM5ErrorCode::Unproven));
	Triangles.Reset();
	AddFloorTile(Triangles, 0, 0, 10, 10);
	AddFloorTile(Triangles, 10, 0, 20, 10);
	AddFloorTile(Triangles, 0, 10, 10, 20);
	AddFloorTile(Triangles, 10, 10, 20, 20);
	TestEqual(TEXT("four touching tiles cover footprint"), int32(Check(Footprint)), int32(EVoxelLandM5ErrorCode::None));
	Triangles.RemoveAt(7);
	TestEqual(TEXT("missing half of a tile rejected"), int32(Check(Footprint)), int32(EVoxelLandM5ErrorCode::Unproven));
	Triangles.Reset();
	AddFloorTile(Triangles, 0, 0, 20, 20);
	Swap(Triangles[0].B, Triangles[0].C);
	TestEqual(TEXT("downward winding rejected"), int32(Check(Footprint)), int32(EVoxelLandM5ErrorCode::Unproven));
	Swap(Triangles[0].B, Triangles[0].C);
	Triangles[0].B.Z = 1;
	TestEqual(TEXT("sloped triangle rejected"), int32(Check(Footprint)), int32(EVoxelLandM5ErrorCode::Unproven));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5FloorSupportFailureTest, "VoxelLand.M5.FloorSupport.Failures", M5FloorSupportFlags)
bool FVoxelLandM5FloorSupportFailureTest::RunTest(const FString& Parameters)
{
	using VoxelLand::M5::FloorSupport::CheckHorizontalRectangularSupport;
	const auto NeverCancel = []() {
		return false;
	};
	TArray<FVoxelLandM5FloorTriangleCm> Triangles;
	AddFloorTile(Triangles, MIN_int64, MIN_int64, MAX_int64, MAX_int64);
	const auto Extreme = FloorRect(MIN_int64, MIN_int64, MAX_int64, MAX_int64);
	TestEqual(TEXT("full-width exact comparisons do not overflow"),
		int32(CheckHorizontalRectangularSupport(Extreme, Triangles, 100, NeverCancel).Code),
		int32(EVoxelLandM5ErrorCode::None));
	TestEqual(TEXT("budget exhausted"),
		int32(CheckHorizontalRectangularSupport(Extreme, Triangles, 0, NeverCancel).Code),
		int32(EVoxelLandM5ErrorCode::BudgetExceeded));
	TestEqual(TEXT("degenerate footprint"),
		int32(CheckHorizontalRectangularSupport(FloorRect(0, 0, 0, 1), Triangles, 100, NeverCancel).Code),
		int32(EVoxelLandM5ErrorCode::InvalidInput));
	int32 Calls = 0;
	const auto CancelDuringScan = [&Calls]() {
		return ++Calls == 4;
	};
	TestEqual(TEXT("cancellation in pair scan"),
		int32(CheckHorizontalRectangularSupport(Extreme, Triangles, 100, CancelDuringScan).Code),
		int32(EVoxelLandM5ErrorCode::Cancelled));
	Triangles.Reset();
	TestEqual(TEXT("missing support"),
		int32(CheckHorizontalRectangularSupport(Extreme, Triangles, 100, NeverCancel).Code),
		int32(EVoxelLandM5ErrorCode::Unproven));
	return true;
}
#endif
