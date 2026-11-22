#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Structs/VoxelLandM5PointCm.h"
#include "Generation/VoxelLandM5IntegerGeometry.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5IntegerGeometryFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandM5PointCm M5IntegerPoint(int64 X, int64 Y, int64 Z)
{
	FVoxelLandM5PointCm P;
	P.X = X;
	P.Y = Y;
	P.Z = Z;
	return P;
}

// Oracle for deliberately small coordinates: use the cross-product identity,
// then find the root by incrementing. All products fit in int64 in this test.
int64 M5SmallSegmentReference(
	const FVoxelLandM5PointCm& P, const FVoxelLandM5PointCm& A, const FVoxelLandM5PointCm& B, int64 Radius)
{
	const int64 Vx = B.X - A.X, Vy = B.Y - A.Y, Vz = B.Z - A.Z;
	const int64 Wx = P.X - A.X, Wy = P.Y - A.Y, Wz = P.Z - A.Z;
	const int64 Length = Vx * Vx + Vy * Vy + Vz * Vz;
	const int64 Dot = Wx * Vx + Wy * Vy + Wz * Vz;
	int64 Numerator = 0;
	int64 Denominator = 1;
	if (Dot <= 0)
	{
		Numerator = Wx * Wx + Wy * Wy + Wz * Wz;
	}
	else if (Dot >= Length)
	{
		const int64 X = P.X - B.X, Y = P.Y - B.Y, Z = P.Z - B.Z;
		Numerator = X * X + Y * Y + Z * Z;
	}
	else
	{
		const int64 X = Wy * Vz - Wz * Vy, Y = Wz * Vx - Wx * Vz, Z = Wx * Vy - Wy * Vx;
		Numerator = X * X + Y * Y + Z * Z;
		Denominator = Length;
	}
	int64 Root = 0;
	while ((Root + 1) * (Root + 1) * Denominator <= Numerator * 65536)
	{
		++Root;
	}
	return Root - Radius * 256;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5IntegerGeometryReferenceTest, "VoxelLand.M5.Numeric.IntegerGeometry.Reference", M5IntegerGeometryFlags)
bool FVoxelLandM5IntegerGeometryReferenceTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::IntegerGeometry;
	const FVoxelLandM5PointCm O = M5IntegerPoint(0, 0, 0);
	int64 Out = 123;
	TestTrue(TEXT("sphere center"), TrySphereDistanceQ8(O, O, 2, Out));
	TestEqual(TEXT("sphere interior"), Out, int64(-512));
	TestTrue(TEXT("sphere 3-4-5"), TrySphereDistanceQ8(M5IntegerPoint(3, 4, 0), O, 2, Out));
	TestEqual(TEXT("sphere exact 3 cm"), Out, int64(768));
	TestTrue(TEXT("sphere irrational"), TrySphereDistanceQ8(M5IntegerPoint(1, 1, 0), O, 1, Out));
	TestEqual(TEXT("floor sqrt(2) in Q8 minus 1"), Out, int64(106));
	TestTrue(
		TEXT("diagonal interior"), TrySegmentDistanceQ8(M5IntegerPoint(0, 2, 0), O, M5IntegerPoint(2, 2, 0), 1, Out));
	TestEqual(TEXT("diagonal sqrt(2)"), Out, int64(106));
	TestTrue(
		TEXT("oblique interior"), TrySegmentDistanceQ8(M5IntegerPoint(3, 2, 1), O, M5IntegerPoint(7, 4, -3), 1, Out));
	TestEqual(TEXT("oblique cross-product reference"), Out, int64(308));
	TestTrue(TEXT("negative point and oblique segment"),
		TrySegmentDistanceQ8(M5IntegerPoint(-8, 3, 9), M5IntegerPoint(-7, -3, 1), M5IntegerPoint(9, 8, -5), 2, Out));
	TestEqual(TEXT("near endpoint cross-product reference"), Out, int64(2060));
	TestTrue(
		TEXT("first endpoint"), TrySegmentDistanceQ8(M5IntegerPoint(-3, 4, 0), O, M5IntegerPoint(10, 0, 0), 2, Out));
	TestEqual(TEXT("first endpoint 3-4-5"), Out, int64(768));
	TestTrue(
		TEXT("second endpoint"), TrySegmentDistanceQ8(M5IntegerPoint(13, 4, 0), O, M5IntegerPoint(10, 0, 0), 2, Out));
	TestEqual(TEXT("second endpoint 3-4-5"), Out, int64(768));
	const FVoxelLandM5PointCm Min = M5IntegerPoint(-1048576, -1048576, -1048576);
	const FVoxelLandM5PointCm Max = M5IntegerPoint(1048576, 1048576, 1048576);
	TestTrue(TEXT("maximum sphere separation"), TrySphereDistanceQ8(Max, Min, 1, Out));
	TestEqual(TEXT("integer sqrt of 3 * (2^21 * 256)^2 minus 256"), Out, int64(929887440));
	TestTrue(TEXT("large segment endpoint"),
		TrySegmentDistanceQ8(Max, Min, M5IntegerPoint(-1048576, 1048576, -1048576), 1, Out));
	TestEqual(TEXT("large endpoint independent result"), Out, int64(759249868));
	TestTrue(
		TEXT("large interior requires wide products"), TrySegmentDistanceQ8(M5IntegerPoint(1048576, -1048576, 1048576),
														   Min, M5IntegerPoint(1048576, 1048576, -1048576), 1, Out));
	TestEqual(TEXT("wide rational reference"), Out, int64(657529640));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5IntegerGeometryOracleTest, "VoxelLand.M5.Numeric.IntegerGeometry.SmallOracle", M5IntegerGeometryFlags)
bool FVoxelLandM5IntegerGeometryOracleTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::IntegerGeometry;
	const FVoxelLandM5PointCm A = M5IntegerPoint(-2, -1, 1);
	const FVoxelLandM5PointCm B = M5IntegerPoint(2, 1, -2);
	for (int64 X = -3; X <= 3; ++X)
	{
		for (int64 Y = -3; Y <= 3; ++Y)
		{
			for (int64 Z = -3; Z <= 3; ++Z)
			{
				const FVoxelLandM5PointCm P = M5IntegerPoint(X, Y, Z);
				const int64 Expected = M5SmallSegmentReference(P, A, B, 1);
				int64 Actual = 123456;
				if (!TrySegmentDistanceQ8(P, A, B, 1, Actual) || Actual != Expected)
				{
					AddError(
						FString::Printf(TEXT("(%lld,%lld,%lld): expected %lld, got %lld"), X, Y, Z, Expected, Actual));
					return false;
				}
				int64 Reversed = 123456;
				if (!TrySegmentDistanceQ8(P, B, A, 1, Reversed) || Reversed != Expected)
				{
					AddError(FString::Printf(
						TEXT("reversed (%lld,%lld,%lld): expected %lld, got %lld"), X, Y, Z, Expected, Reversed));
					return false;
				}
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5IntegerGeometryInvalidTest, "VoxelLand.M5.Numeric.IntegerGeometry.Invalid", M5IntegerGeometryFlags)
bool FVoxelLandM5IntegerGeometryInvalidTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::IntegerGeometry;
	const FVoxelLandM5PointCm O = M5IntegerPoint(0, 0, 0);
	const FVoxelLandM5PointCm B = M5IntegerPoint(1, 0, 0);
	int64 Out = 987654;
	TestFalse(TEXT("zero radius"), TrySphereDistanceQ8(O, O, 0, Out));
	TestFalse(TEXT("negative radius"), TrySphereDistanceQ8(O, O, -1, Out));
	TestFalse(TEXT("radius above limit"), TrySphereDistanceQ8(O, O, 1048577, Out));
	TestFalse(TEXT("out-of-range positive coordinate"), TrySphereDistanceQ8(M5IntegerPoint(1048577, 0, 0), O, 1, Out));
	TestFalse(
		TEXT("out-of-range negative coordinate"), TrySphereDistanceQ8(O, M5IntegerPoint(0, MIN_int64, 0), 1, Out));
	TestFalse(TEXT("coincident segment"), TrySegmentDistanceQ8(O, O, O, 1, Out));
	TestFalse(
		TEXT("out-of-range segment endpoint"), TrySegmentDistanceQ8(O, B, M5IntegerPoint(0, 0, MAX_int64), 1, Out));
	TestFalse(TEXT("segment invalid radius"), TrySegmentDistanceQ8(O, O, B, MIN_int64, Out));
	TestEqual(TEXT("all failures preserve output"), Out, int64(987654));
	return true;
}
#endif
