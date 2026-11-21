#if WITH_DEV_AUTOMATION_TESTS

#include "Generation/VoxelLandM4Control.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace
{
constexpr EAutomationTestFlags M4ControlFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4ControlQuantizationTest, "VoxelLand.M4.Control.Quantization", M4ControlFlags)
bool FVoxelLandM4ControlQuantizationTest::RunTest(const FString& Parameters)
{
	// Exactly representable inputs with independently calculated expected outputs.
	const float Inputs[] = {-2.0f, -1.0f, -0.5f, -1.0f / 65536.0f, 0.0f, 1.0f / 65536.0f, 0.5f, 1.0f, 2.0f};
	const uint16 Expected[] = {0, 0, 16384, 32767, 32768, 32768, 49151, 65535, 65535};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Inputs); ++Index)
	{
		uint16 Value = 42;
		if (TestTrue(FString::Printf(TEXT("finite control %d accepted"), Index),
				VoxelLandM4Control::Quantize(Inputs[Index], Value)))
		{
			TestEqual(FString::Printf(TEXT("control quantization %d"), Index), Value, Expected[Index]);
		}
	}
	uint16 Output = 123;
	TestFalse(TEXT("NaN rejected"), VoxelLandM4Control::Quantize(std::numeric_limits<float>::quiet_NaN(), Output));
	TestEqual(TEXT("NaN preserves output"), Output, uint16(123));
	TestFalse(TEXT("positive infinity rejected"),
		VoxelLandM4Control::Quantize(std::numeric_limits<float>::infinity(), Output));
	TestFalse(TEXT("negative infinity rejected"),
		VoxelLandM4Control::Quantize(-std::numeric_limits<float>::infinity(), Output));
	TestEqual(TEXT("infinities preserve output"), Output, uint16(123));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4ControlLatticeTest, "VoxelLand.M4.Control.Lattice", M4ControlFlags)
bool FVoxelLandM4ControlLatticeTest::RunTest(const FString& Parameters)
{
	int64 Position = 7;
	TestTrue(TEXT("negative voxel index"), VoxelLandM4Control::LatticePositionCm(-9, 100, Position));
	TestEqual(TEXT("negative voxel world position"), Position, int64(-900));
	TestTrue(TEXT("minimum int64 world position"),
		VoxelLandM4Control::LatticePositionCm(std::numeric_limits<int64>::min(), 1, Position));
	TestEqual(TEXT("minimum exact"), Position, std::numeric_limits<int64>::min());
	Position = 7;
	TestFalse(TEXT("negative product overflow"),
		VoxelLandM4Control::LatticePositionCm(std::numeric_limits<int64>::min(), 2, Position));
	TestFalse(TEXT("positive product overflow"),
		VoxelLandM4Control::LatticePositionCm(std::numeric_limits<int64>::max(), 2, Position));
	TestFalse(TEXT("zero voxel size rejected"), VoxelLandM4Control::LatticePositionCm(5, 0, Position));
	TestFalse(TEXT("negative voxel size rejected"), VoxelLandM4Control::LatticePositionCm(5, -1, Position));
	TestEqual(TEXT("invalid product preserves output"), Position, int64(7));

	int64 Index = 7, Remainder = 8;
	TestTrue(TEXT("negative offset"), VoxelLandM4Control::GridCoordinate(-101, 0, 100, Index, Remainder));
	TestEqual(TEXT("floor below zero"), Index, int64(-2));
	TestEqual(TEXT("positive remainder below zero"), Remainder, int64(99));
	TestTrue(
		TEXT("negative origin exact endpoint"), VoxelLandM4Control::GridCoordinate(-700, -900, 100, Index, Remainder));
	TestEqual(TEXT("endpoint index"), Index, int64(2));
	TestEqual(TEXT("endpoint remainder"), Remainder, int64(0));
	TestTrue(TEXT("one before negative origin"), VoxelLandM4Control::GridCoordinate(-901, -900, 100, Index, Remainder));
	TestEqual(TEXT("one before origin index"), Index, int64(-1));
	TestEqual(TEXT("one before origin remainder"), Remainder, int64(99));
	TestTrue(TEXT("minimum offset with unit spacing"),
		VoxelLandM4Control::GridCoordinate(std::numeric_limits<int64>::min(), 0, 1, Index, Remainder));
	TestEqual(TEXT("minimum index exact"), Index, std::numeric_limits<int64>::min());
	TestEqual(TEXT("minimum index remainder"), Remainder, int64(0));
	Index = 7;
	Remainder = 8;
	TestFalse(TEXT("negative subtraction overflow"),
		VoxelLandM4Control::GridCoordinate(std::numeric_limits<int64>::min(), 1, 1, Index, Remainder));
	TestFalse(TEXT("positive subtraction overflow"),
		VoxelLandM4Control::GridCoordinate(std::numeric_limits<int64>::max(), -1, 1, Index, Remainder));
	TestFalse(TEXT("zero spacing rejected"), VoxelLandM4Control::GridCoordinate(0, 0, 0, Index, Remainder));
	TestFalse(TEXT("negative spacing rejected"), VoxelLandM4Control::GridCoordinate(0, 0, -1, Index, Remainder));
	TestEqual(TEXT("failure preserves index"), Index, int64(7));
	TestEqual(TEXT("failure preserves remainder"), Remainder, int64(8));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4ControlBilinearTest, "VoxelLand.M4.Control.Bilinear", M4ControlFlags)
bool FVoxelLandM4ControlBilinearTest::RunTest(const FString& Parameters)
{
	int32 Result = 99;
	TestTrue(TEXT("origin accepted"), VoxelLandM4Control::BilinearHeightQ8(-8, 4, 8, 12, 0, 0, 2, Result));
	TestEqual(TEXT("origin exact"), Result, -8);
	TestTrue(TEXT("upper endpoint accepted"), VoxelLandM4Control::BilinearHeightQ8(-8, 4, 8, 12, 2, 2, 2, Result));
	TestEqual(TEXT("upper endpoint exact"), Result, 12);
	TestTrue(TEXT("middle accepted"), VoxelLandM4Control::BilinearHeightQ8(-8, 4, 8, 12, 1, 1, 2, Result));
	TestEqual(TEXT("four-corner average"), Result, 4);
	TestTrue(TEXT("negative half accepted"), VoxelLandM4Control::BilinearHeightQ8(-1, 0, 0, 0, 1, 0, 2, Result));
	TestEqual(TEXT("negative tie rounds away"), Result, -1);
	TestTrue(TEXT("positive half accepted"), VoxelLandM4Control::BilinearHeightQ8(1, 0, 0, 0, 1, 0, 2, Result));
	TestEqual(TEXT("positive tie rounds away"), Result, 1);
	TestTrue(TEXT("negative heights accepted"), VoxelLandM4Control::BilinearHeightQ8(-20, -12, -8, 0, 1, 1, 2, Result));
	TestEqual(TEXT("negative average"), Result, -10);
	TestTrue(TEXT("maximum allowed heights accepted"),
		VoxelLandM4Control::BilinearHeightQ8(512000000, 512000000, 512000000, 512000000, 1, 1, 2, Result));
	TestEqual(TEXT("maximum height preserved"), Result, 512000000);
	Result = 99;
	TestFalse(TEXT("negative remainder rejected"), VoxelLandM4Control::BilinearHeightQ8(0, 0, 0, 0, -1, 0, 2, Result));
	TestFalse(
		TEXT("remainder outside cell rejected"), VoxelLandM4Control::BilinearHeightQ8(0, 0, 0, 0, 3, 0, 2, Result));
	TestFalse(TEXT("zero spacing rejected"), VoxelLandM4Control::BilinearHeightQ8(0, 0, 0, 0, 0, 0, 0, Result));
	TestFalse(TEXT("height beyond limit rejected"),
		VoxelLandM4Control::BilinearHeightQ8(512000001, 0, 0, 0, 0, 0, 2, Result));
	TestFalse(TEXT("unsafe intermediate rejected"),
		VoxelLandM4Control::BilinearHeightQ8(0, 0, 0, 0, 0, 0, std::numeric_limits<int64>::max(), Result));
	TestFalse(
		TEXT("unsafe weighted sum rejected"), VoxelLandM4Control::BilinearHeightQ8(0, 0, 0, 0, 0, 0, 1000000, Result));
	TestEqual(TEXT("invalid input preserves output"), Result, 99);
	return true;
}
#endif
