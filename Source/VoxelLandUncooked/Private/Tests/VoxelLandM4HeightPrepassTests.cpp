#if WITH_DEV_AUTOMATION_TESTS

#include "Generation/VoxelLandM4CurveLut.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Generation/VoxelLandM4HeightPrepass.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace
{
constexpr EAutomationTestFlags M4HeightPrepassFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

bool M4HeightPrepassCreateCurve(
	const TArray<int32>& Samples, TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe>& Out)
{
	FString Error;
	return FVoxelLandM4CurveLut::Create(
		Samples, std::numeric_limits<int32>::min(), std::numeric_limits<int32>::max(), Out, Error);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4HeightPrepassBaseTest, "VoxelLand.M4.HeightPrepass.Base", M4HeightPrepassFlags)
bool FVoxelLandM4HeightPrepassBaseTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4DomainSettings Domain;
	Domain.CellsX = Domain.CellsY = 1;
	Domain.GridSpacingCm = 100;
	Domain.OriginXCm = Domain.OriginYCm = -100;
	Domain.bGlobalErosion = false;
	TArray<int32> ContinentalSamples, MountainSamples, PeaksSamples;
	for (int32 Index = 0; Index < FVoxelLandM4CurveLut::SampleCount; ++Index)
	{
		ContinentalSamples.Add(Index * 10);
		MountainSamples.Add(-Index * 5);
		PeaksSamples.Add(700);
	}
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> CCurve, MCurve, PCurve;
	if (!TestTrue(TEXT("continental curve"), M4HeightPrepassCreateCurve(ContinentalSamples, CCurve))
		|| !TestTrue(TEXT("mountain curve"), M4HeightPrepassCreateCurve(MountainSamples, MCurve))
		|| !TestTrue(TEXT("peaks curve"), M4HeightPrepassCreateCurve(PeaksSamples, PCurve)))
	{
		return false;
	}
	const TArray<uint16> Continental = {0, 65535, 0, 65535};
	const TArray<uint16> Mountain = {0, 0, 65535, 65535};
	const TArray<uint16> Peaks = {0, 65535, 65535, 0};
	TArray<int32> Heights;
	FVoxelLandError Error = VoxelLandM4HeightPrepass::BuildBaseHeights(
		Domain, Continental, *CCurve, Mountain, *MCurve, Peaks, *PCurve, -100, Heights, [] { return false; });
	TestFalse(TEXT("base generation succeeds"), bool(Error));
	const TArray<int32> Expected = {600, 41560, -19880, 21080};
	TestTrue(TEXT("three control maps and fixed terms produce independent expected values"), Heights == Expected);
	FVoxelLandM4ErosionSettings Erosion;
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	uint64 Hash = 0;
	Error = VoxelLandM4HeightPrepass::Generate(Domain, Continental, *CCurve, Mountain, *MCurve, Peaks, *PCurve, -100,
		42, FGuid(), 0, 0, Erosion, Grid, Hash, [] { return false; });
	if (!TestFalse(TEXT("height prepass succeeds"), bool(Error)) || !TestTrue(TEXT("grid published"), Grid.IsValid()))
	{
		return false;
	}
	int32 Height = 0;
	TestTrue(TEXT("negative origin sample"), Grid->Sample(-100, -100, Height));
	TestEqual(TEXT("first base term survives prepass"), Height, Expected[0]);
	TestTrue(TEXT("inclusive endpoint sample"), Grid->Sample(0, 0, Height));
	TestEqual(TEXT("last base term survives prepass"), Height, Expected[3]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4HeightPrepassSafetyTest, "VoxelLand.M4.HeightPrepass.Safety", M4HeightPrepassFlags)
bool FVoxelLandM4HeightPrepassSafetyTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4DomainSettings Domain;
	Domain.CellsX = Domain.CellsY = 1;
	Domain.GridSpacingCm = 100;
	Domain.bGlobalErosion = false;
	TArray<int32> Samples;
	Samples.Init(0, FVoxelLandM4CurveLut::SampleCount);
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> ZeroCurve;
	if (!TestTrue(TEXT("zero curve"), M4HeightPrepassCreateCurve(Samples, ZeroCurve)))
	{
		return false;
	}
	const TArray<uint16> Controls = {0, 0, 0, 0};
	const TArray<uint16> ShortControls = {0, 0, 0};
	TArray<int32> Heights = {17};
	FVoxelLandError Error = VoxelLandM4HeightPrepass::BuildBaseHeights(Domain, ShortControls, *ZeroCurve, Controls,
		*ZeroCurve, Controls, *ZeroCurve, 0, Heights, [] { return false; });
	TestTrue(TEXT("wrong control size rejected"), bool(Error));
	TestTrue(TEXT("invalid inputs do not publish"), Heights == TArray<int32>({17}));
	Error = VoxelLandM4HeightPrepass::BuildBaseHeights(Domain, Controls, *ZeroCurve, Controls, *ZeroCurve, Controls,
		*ZeroCurve, 512000001, Heights, [] { return false; });
	TestTrue(TEXT("out of range height rejected"), bool(Error));
	TestTrue(TEXT("overflow does not publish"), Heights == TArray<int32>({17}));
	int32 Checks = 0;
	Error = VoxelLandM4HeightPrepass::BuildBaseHeights(Domain, Controls, *ZeroCurve, Controls, *ZeroCurve, Controls,
		*ZeroCurve, 0, Heights, [&Checks] { return ++Checks == 3; });
	TestTrue(TEXT("row cancellation reported"), Error.Code == EVoxelLandErrorCode::Cancelled);
	TestTrue(TEXT("cancellation does not publish"), Heights == TArray<int32>({17}));
	FVoxelLandM4ErosionSettings Erosion;
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	uint64 Hash = 0;
	Error = VoxelLandM4HeightPrepass::Generate(Domain, Controls, *ZeroCurve, Controls, *ZeroCurve, Controls, *ZeroCurve,
		0, 0, FGuid(), 0, 0, Erosion, Grid, Hash, [] { return false; });
	if (!TestFalse(TEXT("baseline prepass succeeds"), bool(Error))
		|| !TestTrue(TEXT("baseline grid exists"), Grid.IsValid()))
	{
		return false;
	}
	const auto OriginalGrid = Grid;
	const uint64 OriginalHash = Hash;
	Error = VoxelLandM4HeightPrepass::Generate(Domain, Controls, *ZeroCurve, ShortControls, *ZeroCurve, Controls,
		*ZeroCurve, 0, 0, FGuid(), 0, 0, Erosion, Grid, Hash, [] { return false; });
	TestTrue(TEXT("invalid map prevents prepass publication"), bool(Error));
	TestTrue(TEXT("failed prepass keeps previous grid"), Grid == OriginalGrid);
	TestEqual(TEXT("failed prepass keeps previous hash"), Hash, OriginalHash);
	Domain.Mode = EVoxelLandM4DomainMode::Unlimited;
	Error = VoxelLandM4HeightPrepass::BuildBaseHeights(
		Domain, Controls, *ZeroCurve, Controls, *ZeroCurve, Controls, *ZeroCurve, 0, Heights, [] { return false; });
	TestTrue(TEXT("unlimited world rejects finite prepass"), bool(Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4HeightPrepassErosionTest, "VoxelLand.M4.HeightPrepass.Erosion", M4HeightPrepassFlags)
bool FVoxelLandM4HeightPrepassErosionTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4DomainSettings Domain;
	Domain.CellsX = Domain.CellsY = 1;
	Domain.GridSpacingCm = 100;
	Domain.OriginXCm = Domain.OriginYCm = 0;
	TArray<int32> Samples;
	Samples.Init(0, FVoxelLandM4CurveLut::SampleCount);
	Samples[0] = 100;
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> SourceCurve, ZeroCurve;
	if (!TestTrue(TEXT("source curve"), M4HeightPrepassCreateCurve(Samples, SourceCurve)))
	{
		return false;
	}
	Samples[0] = 0;
	if (!TestTrue(TEXT("zero curve"), M4HeightPrepassCreateCurve(Samples, ZeroCurve)))
	{
		return false;
	}
	const TArray<uint16> Continental = {0, 65535, 65535, 65535};
	const TArray<uint16> Other = {0, 0, 0, 0};
	FVoxelLandM4ErosionSettings Erosion;
	Erosion.ThermalIterations = 1;
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	uint64 Hash = 0;
	const FVoxelLandError Error = VoxelLandM4HeightPrepass::Generate(Domain, Continental, *SourceCurve, Other,
		*ZeroCurve, Other, *ZeroCurve, 0, 42, FGuid(1, 2, 3, 4), 0, 0, Erosion, Grid, Hash, [] { return false; });
	if (!TestFalse(TEXT("erosion prepass succeeds"), bool(Error))
		|| !TestTrue(TEXT("eroded grid published"), Grid.IsValid()))
	{
		return false;
	}
	int32 Height = 0;
	TestTrue(TEXT("eroded source samples"), Grid->Sample(0, 0, Height));
	TestEqual(TEXT("source loses 25 Q8 units"), Height, 75);
	TestTrue(TEXT("eroded neighbor samples"), Grid->Sample(100, 0, Height));
	TestEqual(TEXT("+X neighbor receives 25 Q8 units"), Height, 25);
	FVoxelLandM4StageStats Stats;
	if (TestTrue(TEXT("erosion observations available"), Grid->GetStageStats(Stats)))
	{
		TestEqual(TEXT("two samples changed"), Stats.ErosionChangedCount, 2);
		TestEqual(TEXT("minimum signed difference"), Stats.ErosionDeltaMinQ8, int64(-25));
		TestEqual(TEXT("maximum signed difference"), Stats.ErosionDeltaMaxQ8, int64(25));
		TestEqual(TEXT("total absolute difference"), Stats.ErosionAbsDeltaSumQ8, uint64(50));
		TestEqual(TEXT("zero input samples counted"), Stats.ContinentalZeroCount, 1);
		TestEqual(TEXT("max input samples counted"), Stats.ContinentalMaxCount, 3);
	}
	return true;
}
#endif
