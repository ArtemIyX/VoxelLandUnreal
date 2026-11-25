#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/Caves/Geometry/VoxelLandCaveProfileGeometry.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5ProfileGeometryTest, "VoxelLand.M5.ProfileGeometry.V2",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5ProfileGeometryTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::ProfileGeometry;
	FSegment Segment;
	Segment.EndCm.X = 100;
	Segment.RadiusCm = 50;
	Segment.StartFloorCm = -20;
	Segment.EndFloorCm = -19;
	Segment.StartCeilingCm = 200;
	Segment.EndCeilingCm = 201;
	FVoxelLandM5PointCm Point;
	Point.X = 50;
	Point.Z = -20;
	int64 Result = 99;
	TestTrue(TEXT("ramp sample"), TrySegmentDistanceQ8(Segment, Point, Result));
	TestEqual(TEXT("half centimetre floor is Q8 exact"), Result, int64(128));
	Point.Z = 50;
	TestTrue(TEXT("interior"), TrySegmentDistanceQ8(Segment, Point, Result));
	TestEqual(TEXT("lateral interior"), Result, int64(-50 * 256));
	Point.Y = 60;
	TestTrue(TEXT("outside side"), TrySegmentDistanceQ8(Segment, Point, Result));
	TestEqual(TEXT("side distance"), Result, int64(10 * 256));
	Point.Y = 0;
	Point.X = 150;
	TestTrue(TEXT("beyond end"), TrySegmentDistanceQ8(Segment, Point, Result));
	TestEqual(TEXT("rounded cap"), Result, int64(0));
	Segment.EndCm.X = 3;
	Segment.StartFloorCm = -1;
	Segment.EndFloorCm = 0;
	Point.X = 1;
	Point.Z = 0;
	TestTrue(TEXT("third fraction"), TrySegmentDistanceQ8(Segment, Point, Result));
	TestEqual(TEXT("positive Q8 interpolation rounds nearest"), Result, int64(-171));
	Segment.StartFloorCm = 0;
	Segment.EndFloorCm = -1;
	TestTrue(TEXT("negative third fraction"), TrySegmentDistanceQ8(Segment, Point, Result));
	TestEqual(TEXT("negative Q8 interpolation rounds nearest"), Result, int64(-85));
	Segment.EndCm.X = 512;
	Segment.StartFloorCm = -1;
	Segment.EndFloorCm = 0;
	Point.X = 1;
	TestTrue(TEXT("negative half-Q8 tie"), TrySegmentDistanceQ8(Segment, Point, Result));
	TestEqual(TEXT("complete negative height rounds away"), Result, int64(-256));
	Segment.StartFloorCm = 1;
	Segment.EndFloorCm = 0;
	Point.Z = 2;
	TestTrue(TEXT("positive half-Q8 tie"), TrySegmentDistanceQ8(Segment, Point, Result));
	TestEqual(TEXT("complete positive height rounds away"), Result, int64(-256));
	Segment.Version = 1;
	Result = 739;
	TestFalse(TEXT("version rejected"), TrySegmentDistanceQ8(Segment, Point, Result));
	TestEqual(TEXT("failure preserves output"), Result, int64(739));
	Segment.Version = 2;
	Segment.EndCm = Segment.StartCm;
	TestFalse(TEXT("vertical segment rejected"), TrySegmentDistanceQ8(Segment, Point, Result));
	FChamber Chamber;
	Chamber.RadiusCm = 100;
	Chamber.FloorCm = -50;
	Chamber.CeilingCm = 150;
	Point = FVoxelLandM5PointCm();
	Point.Z = -51;
	TestTrue(TEXT("chamber floor"), TryChamberDistanceQ8(Chamber, Point, Result));
	TestEqual(TEXT("floor is clipped"), Result, int64(256));
	Chamber.FloorCm = 151;
	Result = 739;
	TestFalse(TEXT("inverted planes rejected"), TryChamberDistanceQ8(Chamber, Point, Result));
	TestEqual(TEXT("chamber failure preserves output"), Result, int64(739));
	return true;
}
#endif
