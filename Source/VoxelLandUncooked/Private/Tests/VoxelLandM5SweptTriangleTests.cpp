#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5SweptTriangle.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5SweptTriangleFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandM5PointCm M5SweepPoint(int64 X, int64 Y, int64 Z)
{
	FVoxelLandM5PointCm Result;
	Result.X = X;
	Result.Y = Y;
	Result.Z = Z;
	return Result;
}

FVoxelLandM5ValidationTriangle M5SweepTriangle(const FVector3d& A, const FVector3d& B, const FVector3d& C)
{
	FVoxelLandM5ValidationTriangle Result;
	Result.A = A;
	Result.B = B;
	Result.C = C;
	return Result;
}

TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> M5SweepBudget(int64 Work)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Result;
	FVoxelLandM5Budget::TryCreate(0, Work, Result);
	return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5SweptTriangleAnalyticTest, "VoxelLand.M5.Sweep.TriangleAnalytic", M5SweptTriangleFlags)
bool FVoxelLandM5SweptTriangleAnalyticTest::RunTest(const FString& Parameters)
{
	using VoxelLand::M5::SweptTriangle::Check;
	const FVoxelLandM5PointCm A = M5SweepPoint(0, 0, 0);
	const FVoxelLandM5PointCm B = M5SweepPoint(100, 0, 0);
	const auto NeverCancel = []() {
		return false;
	};
	TArray<FVoxelLandM5ValidationTriangle> Triangles;
	// For a radius-10 capsule swept along X, the side plane y=10 is tangent.
	Triangles.Add(M5SweepTriangle(FVector3d(-20, 11, -20), FVector3d(120, 11, -20), FVector3d(50, 11, 40)));
	const auto Evaluate = [&](int64 Margin, FVector3d Error) {
		const auto Budget = M5SweepBudget(24);
		return Check(A, B, 10, 20, Margin, Error, Triangles, Budget.ToSharedRef(), NeverCancel).Code;
	};
	TestEqual(TEXT("parallel wall one centimetre beyond capsule"), int32(Evaluate(0, FVector3d::ZeroVector)),
		int32(EVoxelLandM5ErrorCode::None));
	Triangles[0] = M5SweepTriangle(FVector3d(-20, 10, -20), FVector3d(120, 10, -20), FVector3d(50, 10, 40));
	TestEqual(TEXT("tangent wall is not certified"), int32(Evaluate(0, FVector3d::ZeroVector)),
		int32(EVoxelLandM5ErrorCode::Unproven));
	Triangles[0] = M5SweepTriangle(FVector3d(-20, 11, -20), FVector3d(120, 11, -20), FVector3d(50, 11, 40));
	TestEqual(TEXT("margin makes wall tangent"), int32(Evaluate(1, FVector3d::ZeroVector)),
		int32(EVoxelLandM5ErrorCode::Unproven));
	TestEqual(TEXT("uncertainty may bring wall to tangent"), int32(Evaluate(0, FVector3d(0, 1, 0))),
		int32(EVoxelLandM5ErrorCode::Unproven));
	// The wall intersects only the middle of the sweep. Both endpoint capsules
	// are disjoint from x=50, so an endpoint-only test would miss it.
	Triangles[0] = M5SweepTriangle(FVector3d(50, -20, -20), FVector3d(50, 20, -20), FVector3d(50, 0, 40));
	TestEqual(TEXT("middle wall is not certified"), int32(Evaluate(0, FVector3d::ZeroVector)),
		int32(EVoxelLandM5ErrorCode::Unproven));
	Triangles[0] = M5SweepTriangle(FVector3d(111, -20, -20), FVector3d(111, 20, -20), FVector3d(111, 0, 40));
	TestEqual(TEXT("past endpoint cap is separated"), int32(Evaluate(0, FVector3d::ZeroVector)),
		int32(EVoxelLandM5ErrorCode::None));
	Triangles[0] = M5SweepTriangle(FVector3d(110, -20, -20), FVector3d(110, 20, -20), FVector3d(110, 0, 40));
	TestEqual(TEXT("endpoint cap tangency is unproven"), int32(Evaluate(0, FVector3d::ZeroVector)),
		int32(EVoxelLandM5ErrorCode::Unproven));
	// Plane x+y=130 has minimum distance 30/sqrt(2)>10 from the
	// complete medial sweep, although its coordinate ranges overlap the sweep.
	Triangles[0] = M5SweepTriangle(FVector3d(100, 30, 0), FVector3d(120, 10, 0), FVector3d(110, 20, 20));
	TestEqual(TEXT("oblique plane has a verified separating witness"), int32(Evaluate(0, FVector3d::ZeroVector)),
		int32(EVoxelLandM5ErrorCode::None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5SweptTriangleFailureTest, "VoxelLand.M5.Sweep.TriangleFailure", M5SweptTriangleFlags)
bool FVoxelLandM5SweptTriangleFailureTest::RunTest(const FString& Parameters)
{
	using VoxelLand::M5::SweptTriangle::Check;
	const auto NeverCancel = []() {
		return false;
	};
	const FVoxelLandM5PointCm Origin = M5SweepPoint(0, 0, 0);
	TArray<FVoxelLandM5ValidationTriangle> Triangles;
	Triangles.Add(M5SweepTriangle(FVector3d(100, 0, 0), FVector3d(100, 10, 0), FVector3d(100, 0, 10)));
	auto Budget = M5SweepBudget(23);
	TestEqual(TEXT("work exhaustion"),
		int32(
			Check(Origin, Origin, 10, 20, 0, FVector3d::ZeroVector, Triangles, Budget.ToSharedRef(), NeverCancel).Code),
		int32(EVoxelLandM5ErrorCode::BudgetExceeded));
	Budget = M5SweepBudget(24);
	TestEqual(TEXT("invalid capsule"),
		int32(
			Check(Origin, Origin, 10, 19, 0, FVector3d::ZeroVector, Triangles, Budget.ToSharedRef(), NeverCancel).Code),
		int32(EVoxelLandM5ErrorCode::InvalidInput));
	TestEqual(TEXT("outside coordinate envelope"),
		int32(Check(M5SweepPoint(MAX_int64, 0, 0), Origin, 10, 20, 0, FVector3d::ZeroVector, Triangles,
			Budget.ToSharedRef(), NeverCancel)
				.Code),
		int32(EVoxelLandM5ErrorCode::InvalidInput));
	TestEqual(TEXT("negative uncertainty is invalid"),
		int32(Check(Origin, Origin, 10, 20, 0, FVector3d(-1, 0, 0), Triangles, Budget.ToSharedRef(), NeverCancel).Code),
		int32(EVoxelLandM5ErrorCode::InvalidInput));
	int32 Calls = 0;
	const auto CancelAtEnd = [&Calls]() {
		return ++Calls == 3;
	};
	TestEqual(TEXT("cancellation before success"),
		int32(
			Check(Origin, Origin, 10, 20, 0, FVector3d::ZeroVector, Triangles, Budget.ToSharedRef(), CancelAtEnd).Code),
		int32(EVoxelLandM5ErrorCode::Cancelled));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5SweptTriangleInteriorTest, "VoxelLand.M5.Sweep.InteriorWalls", M5SweptTriangleFlags)
bool FVoxelLandM5SweptTriangleInteriorTest::RunTest(const FString& Parameters)
{
	using VoxelLand::M5::SweptTriangle::Check;
	const auto NeverCancel = []() {
		return false;
	};
	const FVoxelLandM5PointCm A = M5SweepPoint(-100, 0, 0);
	const FVoxelLandM5PointCm B = M5SweepPoint(100, 0, 0);
	// Each wall contains the medial point (x,0,10) for the corresponding
	// interior path station. This is an analytic collision independent of the
	// implementation's projection or candidate-axis calculations.
	for (int32 X = -95; X <= 95; X += 5)
	{
		TArray<FVoxelLandM5ValidationTriangle> Triangles;
		Triangles.Add(M5SweepTriangle(FVector3d(X, -20, -20), FVector3d(X, 20, -20), FVector3d(X, 0, 40)));
		const auto Budget = M5SweepBudget(24);
		const auto Result = Check(A, B, 10, 20, 0, FVector3d::ZeroVector, Triangles, Budget.ToSharedRef(), NeverCancel);
		if (Result.Code != EVoxelLandM5ErrorCode::Unproven)
		{
			AddError(FString::Printf(TEXT("Intersecting wall at %d was not rejected"), X));
			return false;
		}
	}
	return true;
}
#endif
