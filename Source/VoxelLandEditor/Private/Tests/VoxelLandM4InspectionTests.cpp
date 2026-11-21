#if WITH_DEV_AUTOMATION_TESTS

#include "Graph/VoxelLandM4Inspection.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4InspectionRangeTest, "VoxelLand.M4.Editor.InspectionRange",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM4InspectionRangeTest::RunTest(const FString& Parameters)
{
	int32 MinZ = 7, MaxZ = 9;
	TestTrue(TEXT("flat zero terrain fits"), VoxelLandM4Inspection::VerticalChunkRange(0, 0, 0, 0, MinZ, MaxZ));
	TestEqual(TEXT("zero terrain includes lower clearance"), MinZ, -1);
	TestEqual(TEXT("zero terrain includes upper clearance"), MaxZ, 0);
	const int32 EdgeQ8 = VoxelLandM4Inspection::ChunkSizeCm * 256;
	TestTrue(
		TEXT("positive boundary fits"), VoxelLandM4Inspection::VerticalChunkRange(EdgeQ8, EdgeQ8, 0, 0, MinZ, MaxZ));
	TestEqual(TEXT("positive boundary lower chunk"), MinZ, 0);
	TestEqual(TEXT("positive boundary upper chunk"), MaxZ, 1);
	TestTrue(
		TEXT("negative boundary fits"), VoxelLandM4Inspection::VerticalChunkRange(-EdgeQ8, -EdgeQ8, 0, 0, MinZ, MaxZ));
	TestEqual(TEXT("negative floor division lower chunk"), MinZ, -2);
	TestEqual(TEXT("negative floor division upper chunk"), MaxZ, -1);
	MinZ = 7;
	MaxZ = 9;
	TestFalse(TEXT("large global range cannot be silently clipped"),
		VoxelLandM4Inspection::VerticalChunkRange(-8 * EdgeQ8, 8 * EdgeQ8, 0, 0, MinZ, MaxZ));
	TestEqual(TEXT("rejection keeps min output"), MinZ, 7);
	TestEqual(TEXT("rejection keeps max output"), MaxZ, 9);
	TestFalse(
		TEXT("inverted height range rejected"), VoxelLandM4Inspection::VerticalChunkRange(1, -1, 0, 0, MinZ, MaxZ));
	TestFalse(TEXT("invalid band rejected"), VoxelLandM4Inspection::VerticalChunkRange(0, 0, 1280001, 0, MinZ, MaxZ));
	TestFalse(TEXT("height and halo outside supported Z range rejected"),
		VoxelLandM4Inspection::VerticalChunkRange(512000000, 512000000, 0, 0, MinZ, MaxZ));
	const int32 ClearanceQ8 = 2 * VoxelLandM4Inspection::VoxelSizeCm * 256;
	const int32 DetailQ8 = 1280000;
	const int32 NearPositive = EdgeQ8 - ClearanceQ8 - DetailQ8 / 2;
	TestTrue(TEXT("height without detail stays below boundary"),
		VoxelLandM4Inspection::VerticalChunkRange(NearPositive, NearPositive, 0, 0, MinZ, MaxZ));
	TestEqual(TEXT("base upper chunk"), MaxZ, 0);
	TestTrue(TEXT("height detail extends above boundary"),
		VoxelLandM4Inspection::VerticalChunkRange(NearPositive, NearPositive, 0, DetailQ8, MinZ, MaxZ));
	TestEqual(TEXT("detail upper chunk"), MaxZ, 1);
	TestTrue(TEXT("height detail extends below negative boundary"),
		VoxelLandM4Inspection::VerticalChunkRange(-NearPositive, -NearPositive, 0, DetailQ8, MinZ, MaxZ));
	TestEqual(TEXT("detail lower chunk"), MinZ, -2);
	const int32 CapHigh = 8 * EdgeQ8 - ClearanceQ8 - DetailQ8 / 2;
	TestTrue(TEXT("range without detail fits vertical cap"),
		VoxelLandM4Inspection::VerticalChunkRange(-7 * EdgeQ8, CapHigh, 0, 0, MinZ, MaxZ));
	MinZ = 7;
	MaxZ = 9;
	TestFalse(TEXT("detail beyond vertical cap is rejected"),
		VoxelLandM4Inspection::VerticalChunkRange(-7 * EdgeQ8, CapHigh, 0, DetailQ8, MinZ, MaxZ));
	TestEqual(TEXT("detail rejection keeps min output"), MinZ, 7);
	TestEqual(TEXT("detail rejection keeps max output"), MaxZ, 9);
	TestFalse(
		TEXT("invalid detail bound rejected"), VoxelLandM4Inspection::VerticalChunkRange(0, 0, 0, 1280001, MinZ, MaxZ));
	const FVector Target(12800.0, 12800.0, 12345.0);
	const FVector Camera = VoxelLandM4Inspection::CameraLocation(Target);
	TestEqual(TEXT("camera is exactly two kilometres horizontally from target"),
		FVector2D(Camera.X - Target.X, Camera.Y - Target.Y).Size(), 200000.0);
	TestEqual(TEXT("horizontal inspection field of view"), VoxelLandM4Inspection::HorizontalFovDegrees, 20.f);
	return true;
}

#endif
