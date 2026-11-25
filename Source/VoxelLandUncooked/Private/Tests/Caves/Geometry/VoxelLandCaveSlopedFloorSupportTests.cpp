#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/Caves/Geometry/VoxelLandCaveSlopedFloorSupport.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5SlopedSupportFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandM5PointCm SlopedPoint(int64 X, int64 Y, int64 Z)
{
	FVoxelLandM5PointCm P;
	P.X = X;
	P.Y = Y;
	P.Z = Z;
	return P;
}

FVoxelLandM5ClosedBoxCm SlopedFootprint(int64 X0, int64 Y0, int64 X1, int64 Y1)
{
	FVoxelLandM5ClosedBoxCm Box;
	Box.Min = SlopedPoint(X0, Y0, 0);
	Box.Max = SlopedPoint(X1, Y1, 0);
	return Box;
}

void AddHalfSlopeTile(TArray<FVoxelLandM5FloorTriangleCm>& Triangles, int64 X0, int64 Y0, int64 X1, int64 Y1)
{
	FVoxelLandM5FloorTriangleCm A;
	A.A = SlopedPoint(X0, Y0, X0 / 2);
	A.B = SlopedPoint(X1, Y0, X1 / 2);
	A.C = SlopedPoint(X1, Y1, X1 / 2);
	FVoxelLandM5FloorTriangleCm B;
	B.A = A.A;
	B.B = A.C;
	B.C = SlopedPoint(X0, Y1, X0 / 2);
	Triangles.Add(A);
	Triangles.Add(B);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5SlopedSupportTest, "VoxelLand.M5.SlopedFloorSupport.Analytic", M5SlopedSupportFlags)

bool FVoxelLandM5SlopedSupportTest::RunTest(const FString& Parameters)
{
	using VoxelLand::M5::SlopedFloorSupport::CheckCoplanarRectangularSupport;
	const auto NeverCancel = [] {
		return false;
	};
	FVoxelLandM5SupportPlane Plane;
	Plane.GradientX = 1;
	Plane.Denominator = 2;
	const auto Footprint = SlopedFootprint(-20, -10, 20, 10);
	TArray<FVoxelLandM5FloorTriangleCm> Triangles;
	AddHalfSlopeTile(Triangles, -20, -10, 0, 10);
	AddHalfSlopeTile(Triangles, 0, -10, 20, 10);
	const auto Check = [&](int32 N, int32 D) {
		return CheckCoplanarRectangularSupport(Footprint, Plane, N, D, Triangles, 1000, NeverCancel).Code;
	};
	TestEqual(TEXT("slope exactly one half and full tiles"), int32(Check(1, 2)), int32(EVoxelLandM5ErrorCode::None));
	TestEqual(
		TEXT("one third bound rejects one half slope"), int32(Check(1, 3)), int32(EVoxelLandM5ErrorCode::Unproven));
	Triangles.RemoveAt(3);
	TestEqual(TEXT("missing half tile rejected"), int32(Check(1, 2)), int32(EVoxelLandM5ErrorCode::Unproven));
	Triangles.Reset();
	AddHalfSlopeTile(Triangles, -20, -10, -2, 10);
	AddHalfSlopeTile(Triangles, 2, -10, 20, 10);
	TestEqual(TEXT("gap between tiles rejected"), int32(Check(1, 2)), int32(EVoxelLandM5ErrorCode::Unproven));
	Triangles.Reset();
	AddHalfSlopeTile(Triangles, -20, -10, 20, 10);
	++Triangles[0].A.Z;
	TestEqual(TEXT("point off exact plane rejected"), int32(Check(1, 2)), int32(EVoxelLandM5ErrorCode::Unproven));
	--Triangles[0].A.Z;
	Swap(Triangles[0].B, Triangles[0].C);
	TestEqual(TEXT("downward winding rejected"), int32(Check(1, 2)), int32(EVoxelLandM5ErrorCode::Unproven));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5SlopedSupportBoundaryTest, "VoxelLand.M5.SlopedFloorSupport.Boundaries", M5SlopedSupportFlags)

bool FVoxelLandM5SlopedSupportBoundaryTest::RunTest(const FString& Parameters)
{
	using VoxelLand::M5::SlopedFloorSupport::CheckCoplanarRectangularSupport;
	const auto NeverCancel = [] {
		return false;
	};
	FVoxelLandM5SupportPlane Plane;
	Plane.GradientX = 1;
	Plane.Denominator = 2;
	TArray<FVoxelLandM5FloorTriangleCm> Triangles;
	AddHalfSlopeTile(Triangles, 0, 0, 20, 20);
	const auto Footprint = SlopedFootprint(0, 0, 20, 20);
	TestEqual(TEXT("preprocessing budget enforced"),
		int32(CheckCoplanarRectangularSupport(Footprint, Plane, 1, 2, Triangles, 1, NeverCancel).Code),
		int32(EVoxelLandM5ErrorCode::BudgetExceeded));
	int32 Calls = 0;
	const auto CancelDuringScan = [&Calls] {
		return ++Calls == 2;
	};
	TestEqual(TEXT("cancellation during triangle preprocessing"),
		int32(CheckCoplanarRectangularSupport(Footprint, Plane, 1, 2, Triangles, 100, CancelDuringScan).Code),
		int32(EVoxelLandM5ErrorCode::Cancelled));
	Plane.Denominator = 0;
	TestEqual(TEXT("zero denominator rejected"),
		int32(CheckCoplanarRectangularSupport(Footprint, Plane, 1, 2, Triangles, 100, NeverCancel).Code),
		int32(EVoxelLandM5ErrorCode::InvalidInput));
	Plane.Denominator = 2;
	Plane.Origin.X = MAX_int64;
	TestEqual(TEXT("outside envelope rejected before arithmetic"),
		int32(CheckCoplanarRectangularSupport(Footprint, Plane, 1, 2, Triangles, 100, NeverCancel).Code),
		int32(EVoxelLandM5ErrorCode::InvalidInput));
	Plane.Origin = SlopedPoint(0, 0, 0);
	Plane.GradientX = 32767;
	Plane.GradientY = -32767;
	Plane.Denominator = 32767;
	// The tangent is sqrt(2), so the exact squared comparator must reject
	// a unit bound without overflowing at the maximum supported coefficients.
	TestEqual(TEXT("two-axis extreme gradient exceeds unit slope"),
		int32(CheckCoplanarRectangularSupport(Footprint, Plane, 1, 1, Triangles, 100, NeverCancel).Code),
		int32(EVoxelLandM5ErrorCode::Unproven));
	return true;
}
#endif