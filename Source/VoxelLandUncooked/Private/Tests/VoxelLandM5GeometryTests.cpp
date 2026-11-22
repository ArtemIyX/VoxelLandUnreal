#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5Geometry.h"
#include "Misc/AutomationTest.h"

#include <cmath>
#include <limits>

namespace
{
constexpr EAutomationTestFlags M5GeometryTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5GeometryDistancesTest, "VoxelLand.M5.Geometry.AnalyticDistances", M5GeometryTestFlags)
bool FVoxelLandM5GeometryDistancesTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::Geometry;
	double Result = 99.0;
	TestTrue(TEXT("sphere center"), TrySphereDistance(FVector3d(1, 2, 3), FVector3d(1, 2, 3), 2, Result));
	TestEqual(TEXT("inside is negative"), Result, -2.0);
	TestTrue(TEXT("sphere 3-4-5"), TrySphereDistance(FVector3d(4, 6, 3), FVector3d(1, 2, 3), 2, Result));
	TestEqual(TEXT("sphere distance"), Result, 3.0);
	TestTrue(TEXT("sphere boundary"), TrySphereDistance(FVector3d(3, 2, 3), FVector3d(1, 2, 3), 2, Result));
	TestEqual(TEXT("boundary is zero"), Result, 0.0);
	const FVector3d A(0, 0, 0);
	const FVector3d B(10, 0, 0);
	TestTrue(TEXT("segment side"), TrySegmentDistance(FVector3d(4, 3, 4), A, B, 2, Result));
	TestEqual(TEXT("segment 3-4-5"), Result, 3.0);
	TestTrue(TEXT("first end cap"), TrySegmentDistance(FVector3d(-3, 4, 0), A, B, 2, Result));
	TestEqual(TEXT("first end distance"), Result, 3.0);
	TestTrue(TEXT("second end cap"), TrySegmentDistance(FVector3d(13, 4, 0), A, B, 2, Result));
	TestEqual(TEXT("second end distance"), Result, 3.0);
	TestTrue(TEXT("segment axis"), TrySegmentDistance(FVector3d(5, 0, 0), A, B, 2, Result));
	TestEqual(TEXT("axis inside"), Result, -2.0);
	TestTrue(TEXT("reversed endpoints"), TrySegmentDistance(FVector3d(4, 3, 4), B, A, 2, Result));
	TestEqual(TEXT("reversed distance"), Result, 3.0);
	// Independent geometric construction: the point (0,2,0) projects onto
	// (1,1,0) on the diagonal segment; that distance is exactly sqrt(2).
	TestTrue(TEXT("diagonal segment"), TrySegmentDistance(FVector3d(0, 2, 0), A, FVector3d(2, 2, 0), .5, Result));
	TestTrue(TEXT("diagonal analytic distance"), FMath::Abs(Result - (std::sqrt(2.0) - .5)) < 1.e-14);
	// Translation by 2^40 is exactly representable and leaves the constructed
	// right triangle unchanged; this catches accidental float narrowing.
	const double Base = 1099511627776.0;
	TestTrue(TEXT("large translated diagonal"),
		TrySegmentDistance(FVector3d(Base, Base + 2, Base), FVector3d(Base, Base, Base),
			FVector3d(Base + 2, Base + 2, Base), .5, Result));
	TestTrue(TEXT("translated analytic distance"), FMath::Abs(Result - (std::sqrt(2.0) - .5)) < 1.e-14);
	// The two points are distinct even though their squared separation would
	// underflow. The perpendicular offset has exact length 3 and radius 1.
	TestTrue(TEXT("very short segment"),
		TrySegmentDistance(FVector3d(5.e-201, 3, 0), A, FVector3d(1.e-200, 0, 0), 1, Result));
	TestEqual(TEXT("short segment distance"), Result, 2.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5GeometryBoundsTest, "VoxelLand.M5.Geometry.ConservativeBounds", M5GeometryTestFlags)
bool FVoxelLandM5GeometryBoundsTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::Geometry;
	FBox3d Bounds(FVector3d(-20, -20, -20), FVector3d(20, 20, 20));
	TestTrue(TEXT("sphere bounds"), TrySphereBounds(FVector3d(3, -4, 5), 2, Bounds));
	TestTrue(TEXT("sphere lower extents contained"), Bounds.Min.X <= 1 && Bounds.Min.Y <= -6 && Bounds.Min.Z <= 3);
	TestTrue(TEXT("sphere upper extents contained"), Bounds.Max.X >= 5 && Bounds.Max.Y >= -2 && Bounds.Max.Z >= 7);
	TestTrue(TEXT("segment bounds"), TrySegmentBounds(FVector3d(3, -4, 5), FVector3d(-3, 4, -5), 2, Bounds));
	TestTrue(TEXT("segment lower extents contained"), Bounds.Min.X <= -5 && Bounds.Min.Y <= -6 && Bounds.Min.Z <= -7);
	TestTrue(TEXT("segment upper extents contained"), Bounds.Max.X >= 5 && Bounds.Max.Y >= 6 && Bounds.Max.Z >= 7);
	// At 2^53, adding one rounds back to the center on IEEE double. The
	// containing maximum must be strictly larger than the represented center.
	const double Large = 9007199254740992.0;
	TestTrue(TEXT("rounding expansion"), TrySphereBounds(FVector3d(Large, 0, 0), 1, Bounds));
	TestTrue(TEXT("upper rounded inward is expanded"), Bounds.Max.X > Large);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5GeometryInvalidTest, "VoxelLand.M5.Geometry.InvalidInputs", M5GeometryTestFlags)
bool FVoxelLandM5GeometryInvalidTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::Geometry;
	const double Nan = std::numeric_limits<double>::quiet_NaN();
	const double Infinity = std::numeric_limits<double>::infinity();
	const double Max = std::numeric_limits<double>::max();
	const FVector3d Zero(0, 0, 0);
	FVector3d NanPoint = Zero;
	NanPoint.X = Nan;
	FVector3d InfinitePoint = Zero;
	InfinitePoint.X = Infinity;
	double Result = 123.0;
	TestFalse(TEXT("negative radius"), TrySphereDistance(Zero, Zero, -1, Result));
	TestFalse(TEXT("zero radius"), TrySphereDistance(Zero, Zero, 0, Result));
	TestFalse(TEXT("nan position"), TrySphereDistance(NanPoint, Zero, 1, Result));
	TestFalse(TEXT("infinite radius"), TrySphereDistance(Zero, Zero, Infinity, Result));
	TestFalse(
		TEXT("overflowing subtraction"), TrySphereDistance(FVector3d(Max, 0, 0), FVector3d(-Max, 0, 0), 1, Result));
	TestFalse(TEXT("zero length"), TrySegmentDistance(Zero, Zero, Zero, 1, Result));
	TestFalse(TEXT("nonfinite end"), TrySegmentDistance(Zero, Zero, InfinitePoint, 1, Result));
	TestEqual(TEXT("distance unchanged on all failures"), Result, 123.0);
	const FBox3d Original(FVector3d(-9, -8, -7), FVector3d(7, 8, 9));
	FBox3d Bounds = Original;
	TestFalse(TEXT("bad bounds radius"), TrySphereBounds(Zero, Nan, Bounds));
	TestFalse(TEXT("unrepresentable bounds"), TrySphereBounds(FVector3d(Max, 0, 0), 1, Bounds));
	TestFalse(TEXT("coincident segment bounds"), TrySegmentBounds(Zero, Zero, 1, Bounds));
	TestTrue(TEXT("bounds unchanged on failure"), Bounds.Min == Original.Min && Bounds.Max == Original.Max);
	return true;
}
#endif
