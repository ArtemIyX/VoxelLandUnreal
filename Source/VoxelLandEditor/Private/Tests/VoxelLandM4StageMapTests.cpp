#if WITH_DEV_AUTOMATION_TESTS
#include "Graph/VoxelLandM4StageMap.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4StageMapTest, "VoxelLand.M4.Editor.StageMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM4StageMapTest::RunTest(const FString& Parameters)
{
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	const TArray<int32> Heights = {0, 100, 200, 300};
	if (!TestTrue(
			TEXT("asymmetric grid creates"), FVoxelLandM4HeightGrid::Create(-100, -200, 1, 1, 100, Heights, Grid)))
	{
		return false;
	}
	TArray<uint8> Pixels;
	if (!TestTrue(TEXT("map builds"), VoxelLandM4StageMap::Build(*Grid, [] { return false; }, Pixels)))
	{
		return false;
	}
	constexpr int32 R = VoxelLandM4StageMap::Resolution;
	if (!TestEqual(TEXT("fixed bounded pixel size"), Pixels.Num(), R * R * 4))
	{
		return false;
	}
	TestEqual(TEXT("top left is min X max Y"), Pixels[0], uint8(170));
	TestEqual(TEXT("top right is max X max Y"), Pixels[(R - 1) * 4], uint8(255));
	TestEqual(TEXT("bottom left is min X min Y"), Pixels[(R - 1) * R * 4], uint8(0));
	TestEqual(TEXT("bottom right is max X min Y"), Pixels[(R * R - 1) * 4], uint8(85));
	TestEqual(TEXT("opaque alpha"), Pixels[3], uint8(255));
	const TArray<uint8> Before = Pixels;
	int32 Calls = 0;
	TestFalse(TEXT("cancellation rejects partial map"),
		VoxelLandM4StageMap::Build(*Grid, [&Calls] { return ++Calls > 2; }, Pixels));
	TestTrue(TEXT("cancelled map preserves previous output"), Pixels == Before);
	if (!TestTrue(TEXT("grid near signed coordinate limits creates"),
			FVoxelLandM4HeightGrid::Create(MIN_int64, MAX_int64 - 100, 1, 1, 100, Heights, Grid)))
	{
		return false;
	}
	if (!TestTrue(
			TEXT("extreme-coordinate map builds"), VoxelLandM4StageMap::Build(*Grid, [] { return false; }, Pixels)))
	{
		return false;
	}
	TestEqual(TEXT("extreme-coordinate map preserves top right"), Pixels[(R - 1) * 4], uint8(255));
	TestEqual(TEXT("extreme-coordinate map preserves bottom left"), Pixels[(R - 1) * R * 4], uint8(0));
	const TArray<int32> Flat = {-123, -123, -123, -123};
	if (!TestTrue(TEXT("constant grid creates"), FVoxelLandM4HeightGrid::Create(0, 0, 1, 1, 100, Flat, Grid)))
	{
		return false;
	}
	if (!TestTrue(TEXT("constant map builds"), VoxelLandM4StageMap::Build(*Grid, [] { return false; }, Pixels)))
	{
		return false;
	}
	for (int32 I = 0; I < Pixels.Num(); I += 4)
	{
		if (!TestTrue(TEXT("constant grid is opaque mid-gray"),
				Pixels[I] == 128 && Pixels[I + 1] == 128 && Pixels[I + 2] == 128 && Pixels[I + 3] == 255))
		{
			return false;
		}
	}
	return true;
}
#endif
