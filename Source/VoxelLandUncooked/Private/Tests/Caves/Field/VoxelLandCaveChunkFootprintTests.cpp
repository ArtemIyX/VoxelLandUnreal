#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Field/Chunks/VoxelLandChunkKey.h"
#include "Data/Caves/Field/VoxelLandCaveChunkFootprintRecord.h"
#include "Generation/Caves/Field/VoxelLandCaveChunkFootprint.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5ChunkFootprintTest, "VoxelLand.M5.Numeric.ChunkFootprint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5ChunkFootprintTest::RunTest(const FString& Parameters)
{
	FVoxelLandChunkKey Key;
	Key.Coordinate = FIntVector(-1, 0, 1);
	FVoxelLandM5ChunkFootprint Footprint;
	if (!TestTrue(TEXT("plain footprint"), VoxelLand::M5::ChunkFootprint::TryBuild(Key, 100, false, false, Footprint)))
	{
		return false;
	}
	TestEqual(TEXT("stored negative X min"), Footprint.StoredMin.X, int64(-3300));
	TestEqual(TEXT("stored negative X max"), Footprint.StoredMax.X, int64(100));
	TestEqual(TEXT("stored Y min"), Footprint.StoredMin.Y, int64(-100));
	TestEqual(TEXT("stored Y max"), Footprint.StoredMax.Y, int64(3300));
	TestEqual(TEXT("stored Z min"), Footprint.StoredMin.Z, int64(3100));
	TestEqual(TEXT("stored Z max"), Footprint.StoredMax.Z, int64(6500));
	TestEqual(TEXT("plain density min"), Footprint.DensityInputMin.X, int64(-3300));
	TestEqual(TEXT("plain height max"), Footprint.HeightInputMax.Y, int64(3300));

	TestTrue(
		TEXT("smooth and slope footprint"), VoxelLand::M5::ChunkFootprint::TryBuild(Key, 100, true, true, Footprint));
	TestEqual(TEXT("smooth X min"), Footprint.DensityInputMin.X, int64(-3400));
	TestEqual(TEXT("smooth X max"), Footprint.DensityInputMax.X, int64(200));
	TestEqual(TEXT("height X min"), Footprint.HeightInputMin.X, int64(-3500));
	TestEqual(TEXT("height Y max"), Footprint.HeightInputMax.Y, int64(3500));
	TestEqual(TEXT("height Z is density Z"), Footprint.HeightInputMax.Z, int64(6600));
	TestEqual(TEXT("stored unchanged by options"), Footprint.StoredMax.X, int64(100));

	TestTrue(
		TEXT("slope without smoothing"), VoxelLand::M5::ChunkFootprint::TryBuild(Key, 100, false, true, Footprint));
	TestEqual(TEXT("slope only X"), Footprint.HeightInputMin.X, int64(-3400));
	TestEqual(TEXT("density without smoothing"), Footprint.DensityInputMin.X, int64(-3300));
	TestTrue(TEXT("mask and owner footprints with sequential filters"),
		VoxelLand::M5::ChunkFootprint::TryBuildExpanded(Key, 100, true, true, 3, 1, Footprint));
	TestEqual(TEXT("owner X minimum"), Footprint.OwnerMin.X, int64(-3400));
	TestEqual(TEXT("owner then mask baseline X minimum"), Footprint.BaselineMin.X, int64(-3700));
	TestEqual(TEXT("owner then mask baseline X maximum"), Footprint.BaselineMax.X, int64(500));
	TestEqual(TEXT("owner mask then smooth X minimum"), Footprint.DensityInputMin.X, int64(-3800));
	TestEqual(TEXT("owner mask smooth then slope X minimum"), Footprint.HeightInputMin.X, int64(-3900));
	TestEqual(TEXT("slope adds no Z stencil"), Footprint.HeightInputMax.Z, int64(7000));
	TestTrue(TEXT("owner and mask halos accumulate"),
		VoxelLand::M5::ChunkFootprint::TryBuildExpanded(Key, 100, false, false, 1, 3, Footprint));
	TestEqual(TEXT("owner plus mask X minimum"), Footprint.BaselineMin.X, int64(-3700));
	TestEqual(TEXT("owner union Z maximum"), Footprint.OwnerMax.Z, int64(6800));
	const FVoxelLandM5ChunkFootprint Before = Footprint;
	TestFalse(TEXT("negative halo rejected"),
		VoxelLand::M5::ChunkFootprint::TryBuildExpanded(Key, 100, false, false, -1, 0, Footprint));
	TestFalse(TEXT("zero size rejected"), VoxelLand::M5::ChunkFootprint::TryBuild(Key, 0, true, true, Footprint));
	TestFalse(TEXT("negative size rejected"), VoxelLand::M5::ChunkFootprint::TryBuild(Key, -1, true, true, Footprint));
	Key.LOD = 1;
	TestFalse(TEXT("LOD rejected"), VoxelLand::M5::ChunkFootprint::TryBuild(Key, 100, true, true, Footprint));
	Key.LOD = 0;
	TestFalse(
		TEXT("overflow rejected"), VoxelLand::M5::ChunkFootprint::TryBuild(Key, MAX_int64, true, true, Footprint));
	TestEqual(TEXT("failure preserves output"), Footprint.HeightInputMin.X, Before.HeightInputMin.X);
	TestEqual(TEXT("failure preserves other bound"), Footprint.StoredMax.Z, Before.StoredMax.Z);
	return true;
}
#endif
