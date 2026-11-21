#if WITH_DEV_AUTOMATION_TESTS

#include "Generation/VoxelLandM4CurveLut.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Generation/VoxelLandM4HeightPrepass.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M4StageInspectorFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4StageInspectorTest, "VoxelLand.M4.StageInspector.ObservedRanges", M4StageInspectorFlags)
bool FVoxelLandM4StageInspectorTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4DomainSettings Domain;
	Domain.CellsX = Domain.CellsY = 1;
	Domain.GridSpacingCm = 100;
	Domain.bGlobalErosion = false;
	TArray<int32> Samples;
	Samples.Init(42, FVoxelLandM4CurveLut::SampleCount);
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
	FString Message;
	if (!TestTrue(TEXT("curve created"), FVoxelLandM4CurveLut::Create(Samples, -1000, 1000, Curve, Message)))
	{
		return false;
	}
	const TArray<uint16> Continental = {100, 200, 300, 400};
	const TArray<uint16> Mountain = {500, 600, 700, 800};
	const TArray<uint16> Peaks = {900, 1000, 1100, 1200};
	FVoxelLandM4ErosionSettings Erosion;
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	uint64 Hash = 0;
	const FVoxelLandError Error = VoxelLandM4HeightPrepass::Generate(Domain, Continental, *Curve, Mountain, *Curve,
		Peaks, *Curve, 10, 42, FGuid(), 0, 0, Erosion, Grid, Hash, [] { return false; });
	if (!TestFalse(TEXT("prepass succeeds"), bool(Error)) || !TestTrue(TEXT("grid exists"), Grid.IsValid()))
	{
		return false;
	}
	FVoxelLandM4StageStats Stats;
	if (!TestTrue(TEXT("observed statistics exist"), Grid->GetStageStats(Stats)))
	{
		return false;
	}
	TestEqual(TEXT("sample count"), Stats.SampleCount, 4);
	TestEqual(TEXT("continental minimum"), int32(Stats.ContinentalMin), 100);
	TestEqual(TEXT("continental maximum"), int32(Stats.ContinentalMax), 400);
	TestEqual(TEXT("mountain minimum"), int32(Stats.MountainMin), 500);
	TestEqual(TEXT("mountain maximum"), int32(Stats.MountainMax), 800);
	TestEqual(TEXT("peaks minimum"), int32(Stats.PeaksMin), 900);
	TestEqual(TEXT("peaks maximum"), int32(Stats.PeaksMax), 1200);
	TestEqual(TEXT("base minimum"), Stats.BaseHeightMinQ8, 136);
	TestEqual(TEXT("base maximum"), Stats.BaseHeightMaxQ8, 136);
	auto SharedGrid = Grid;
	Grid.Reset();
	TestTrue(TEXT("shared grid retains statistics"), SharedGrid->GetStageStats(Stats));
	TestEqual(TEXT("shared statistics remain associated"), int32(Stats.PeaksMax), 1200);
	Grid = SharedGrid;
	const uint64 OriginalHash = Hash;
	const FVoxelLandError Cancelled = VoxelLandM4HeightPrepass::Generate(Domain, Continental, *Curve, Mountain, *Curve,
		Peaks, *Curve, 10, 42, FGuid(), 0, 0, Erosion, Grid, Hash, [] { return true; });
	TestTrue(TEXT("cancelled prepass reports cancellation"), Cancelled.Code == EVoxelLandErrorCode::Cancelled);
	TestTrue(TEXT("cancelled prepass preserves grid and statistics"), Grid == SharedGrid);
	TestEqual(TEXT("cancelled prepass preserves hash"), Hash, OriginalHash);
	const TArray<int32> Heights = {1, 2, 3, 4};
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Legacy;
	if (TestTrue(TEXT("legacy grid created"), FVoxelLandM4HeightGrid::Create(0, 0, 1, 1, 100, Heights, Legacy)))
	{
		Stats.SampleCount = 99;
		TestFalse(TEXT("legacy statistics unavailable"), Legacy->GetStageStats(Stats));
		TestEqual(TEXT("unavailable leaves output unchanged"), Stats.SampleCount, 99);
	}
	return true;
}
#endif
