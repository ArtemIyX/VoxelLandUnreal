#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5RouteCertificate.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5RouteCertificateFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandM5PointCm RoutePoint(int64 X, int64 Y, int64 Z)
{
	FVoxelLandM5PointCm Point;
	Point.X = X;
	Point.Y = Y;
	Point.Z = Z;
	return Point;
}

FVoxelLandM5ClosedBoxCm RouteBox(int64 MinX, int64 MinY, int64 MinZ, int64 MaxX, int64 MaxY, int64 MaxZ)
{
	FVoxelLandM5ClosedBoxCm Box;
	Box.Min = RoutePoint(MinX, MinY, MinZ);
	Box.Max = RoutePoint(MaxX, MaxY, MaxZ);
	return Box;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5RouteSweepTest, "VoxelLand.M5.RouteCertificate.SweptBoxes", M5RouteCertificateFlags)
bool FVoxelLandM5RouteSweepTest::RunTest(const FString& Parameters)
{
	using VoxelLand::M5::RouteCertificate::CheckSweptCapsuleBoxSeparation;
	const auto NeverCancel = []() {
		return false;
	};
	const FVoxelLandM5PointCm A = RoutePoint(0, 0, 0);
	const FVoxelLandM5PointCm B = RoutePoint(100, 0, 0);
	// The independent enclosing sweep is [-12,112] x [-12,12] x [-2,22].
	// It conservatively contains every translated radius-10, height-20 capsule.
	TArray<FVoxelLandM5ClosedBoxCm> Boxes;
	Boxes.Add(RouteBox(50, 13, 0, 50, 13, 20));
	auto Check = [&]() {
		return CheckSweptCapsuleBoxSeparation(A, B, 10, 20, 2, Boxes, 1, NeverCancel).Code;
	};
	TestEqual(TEXT("outside expanded envelope is separated"), int32(Check()), int32(EVoxelLandM5ErrorCode::None));
	Boxes[0] = RouteBox(50, 12, 0, 50, 12, 20);
	TestEqual(TEXT("inclusive side touch is unproven"), int32(Check()), int32(EVoxelLandM5ErrorCode::Unproven));
	Boxes[0] = RouteBox(49, -1, 5, 51, 1, 15);
	TestEqual(
		TEXT("obstacle between path endpoints is unproven"), int32(Check()), int32(EVoxelLandM5ErrorCode::Unproven));
	Boxes[0] = RouteBox(112, 0, 22, 112, 0, 22);
	TestEqual(TEXT("end cap and ceiling bounds inclusive"), int32(Check()), int32(EVoxelLandM5ErrorCode::Unproven));
	Boxes[0] = RouteBox(113, 0, 0, 113, 0, 20);
	TestEqual(TEXT("past end envelope is separated"), int32(Check()), int32(EVoxelLandM5ErrorCode::None));
	// AABB overlap may be a false positive (e.g. diagonally outside the capsule).
	// It must remain Unproven, not be mislabeled as an actual collision.
	Boxes[0] = RouteBox(-12, -12, 10, -12, -12, 10);
	TestEqual(TEXT("conservative corner false positive"), int32(Check()), int32(EVoxelLandM5ErrorCode::Unproven));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5RouteSweepFailureTest, "VoxelLand.M5.RouteCertificate.Failures", M5RouteCertificateFlags)
bool FVoxelLandM5RouteSweepFailureTest::RunTest(const FString& Parameters)
{
	using VoxelLand::M5::RouteCertificate::CheckSweptCapsuleBoxSeparation;
	const auto NeverCancel = []() {
		return false;
	};
	const FVoxelLandM5PointCm Origin = RoutePoint(0, 0, 0);
	TArray<FVoxelLandM5ClosedBoxCm> Boxes;
	Boxes.Add(RouteBox(100, 100, 100, 101, 101, 101));
	const auto Check = [&](const FVoxelLandM5PointCm& Start, int64 Radius, int64 Height, int64 Margin, int64 Budget) {
		return CheckSweptCapsuleBoxSeparation(Start, Origin, Radius, Height, Margin, Boxes, Budget, NeverCancel).Code;
	};
	TestEqual(TEXT("invalid capsule"), int32(Check(Origin, 10, 19, 0, 1)), int32(EVoxelLandM5ErrorCode::InvalidInput));
	TestEqual(TEXT("negative margin"), int32(Check(Origin, 10, 20, -1, 1)), int32(EVoxelLandM5ErrorCode::InvalidInput));
	TestEqual(TEXT("budget exhausted before scan"), int32(Check(Origin, 10, 20, 0, 0)),
		int32(EVoxelLandM5ErrorCode::BudgetExceeded));
	TestEqual(TEXT("extent overflow"), int32(Check(RoutePoint(MIN_int64, 0, 0), 10, 20, 0, 1)),
		int32(EVoxelLandM5ErrorCode::ArithmeticOverflow));
	Boxes[0] = RouteBox(2, 0, 0, 1, 1, 1);
	TestEqual(TEXT("invalid obstacle"), int32(Check(Origin, 10, 20, 0, 1)), int32(EVoxelLandM5ErrorCode::InvalidInput));
	Boxes[0] = RouteBox(100, 100, 100, 101, 101, 101);
	int32 Calls = 0;
	const auto CancelBeforeSuccess = [&Calls]() {
		return ++Calls == 3;
	};
	TestEqual(TEXT("cancellation before publication"),
		int32(CheckSweptCapsuleBoxSeparation(Origin, Origin, 10, 20, 0, Boxes, 1, CancelBeforeSuccess).Code),
		int32(EVoxelLandM5ErrorCode::Cancelled));
	return true;
}
#endif
