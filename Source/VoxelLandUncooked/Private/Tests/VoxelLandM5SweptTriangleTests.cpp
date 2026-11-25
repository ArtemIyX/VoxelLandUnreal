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
	FVoxelLandM5Budget::TryCreate(65536, Work, Result);
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
		const auto Budget = M5SweepBudget(20000);
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
		const auto Budget = M5SweepBudget(20000);
		const auto Result = Check(A, B, 10, 20, 0, FVector3d::ZeroVector, Triangles, Budget.ToSharedRef(), NeverCancel);
		if (Result.Code != EVoxelLandM5ErrorCode::Unproven)
		{
			AddError(FString::Printf(TEXT("Intersecting wall at %d was not rejected"), X));
			return false;
		}
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5SweptTriangleSubdivisionTest,
	"VoxelLand.M5.Sweep.DyadicSubdivision", M5SweptTriangleFlags)
bool FVoxelLandM5SweptTriangleSubdivisionTest::RunTest(const FString& Parameters)
{
	using VoxelLand::M5::SweptTriangle::Check;
	using E = EVoxelLandM5ErrorCode;
	const FVoxelLandM5PointCm A = M5SweepPoint(0, 0, 0), B = M5SweepPoint(100, 0, 0);
	const TArray<FVoxelLandM5ValidationTriangle> Triangles = {M5SweepTriangle(
		FVector3d(33, -26, -13), FVector3d(-36, -8, 7), FVector3d(4, -10, -2))};
	const auto Never = [] { return false; };
	// Independent whole-sweep witness: n=(3,10,10), triangle max n.p=-108,
	// medial minimum n.p=100. Gap 208/sqrt(209)>14 cm > radius 10.
	FVoxelLandM5SweepSubdivisionLimits Root;
	Root.MaxDepth = 0; Root.MaxNodes = 1;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget = M5SweepBudget(24);
	TestEqual(TEXT("whole witness set is inconclusive"), Check(A, B, 10, 20, 0, FVector3d::ZeroVector,
		Triangles, Root, Budget.ToSharedRef(), Never).Code, E::Unproven);
	FVoxelLandM5SweepSubdivisionLimits Half;
	Half.MaxDepth = 1; Half.MaxNodes = 3;
	Budget = M5SweepBudget(72);
	TestEqual(TEXT("both exact dyadic halves separate"), Check(A, B, 10, 20, 0, FVector3d::ZeroVector,
		Triangles, Half, Budget.ToSharedRef(), Never).Code, E::None);
	int64 Live = 0, Peak = 0, Work = 0;
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("three nodes charged"), Work, int64(72));
	TestEqual(TEXT("temporary stack lease released"), Live, int64(0));
	Budget = M5SweepBudget(72);
	TestEqual(TEXT("odd-length sweep separates at half-centimetre midpoint"),
		Check(A, M5SweepPoint(101, 0, 0), 10, 20, 0, FVector3d::ZeroVector,
			Triangles, Half, Budget.ToSharedRef(), Never).Code, E::None);
	Half.MaxNodes = 2;
	Budget = M5SweepBudget(72);
	TestEqual(TEXT("node cap fails closed"), Check(A, B, 10, 20, 0, FVector3d::ZeroVector,
		Triangles, Half, Budget.ToSharedRef(), Never).Code, E::Unproven);
	Half.MaxNodes = 3;
	Budget = M5SweepBudget(71);
	TestEqual(TEXT("one unit short rejected"), Check(A, B, 10, 20, 0, FVector3d::ZeroVector,
		Triangles, Half, Budget.ToSharedRef(), Never).Code, E::BudgetExceeded);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> NoMemory;
	if (!FVoxelLandM5Budget::TryCreate(0, 72, NoMemory)) return false;
	TestEqual(TEXT("stack admission rejected"), Check(A, B, 10, 20, 0, FVector3d::ZeroVector,
		Triangles, Half, NoMemory.ToSharedRef(), Never).Code, E::BudgetExceeded);
	Budget = M5SweepBudget(72);
	int32 Polls = 0;
	TestEqual(TEXT("cancel before first child"), Check(A, B, 10, 20, 0, FVector3d::ZeroVector,
		Triangles, Half, Budget.ToSharedRef(), [&Polls] { return ++Polls == 3; }).Code, E::Cancelled);
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("cancel releases stack"), Live, int64(0));
	return true;
}
#endif
