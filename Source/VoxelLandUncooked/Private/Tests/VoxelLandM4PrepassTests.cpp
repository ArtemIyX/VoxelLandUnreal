#if WITH_DEV_AUTOMATION_TESTS

#include "Generation/VoxelLandM4HeightGrid.h"
#include "Generation/VoxelLandM4Prepass.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M4PrepassTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4PrepassSnapshotTest, "VoxelLand.M4.Prepass.Snapshot", M4PrepassTestFlags)
bool FVoxelLandM4PrepassSnapshotTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4DomainSettings Domain;
	Domain.CellsX = 1;
	Domain.CellsY = 1;
	Domain.OriginXCm = -100;
	Domain.OriginYCm = -100;
	Domain.GridSpacingCm = 100;
	Domain.bGlobalErosion = false;
	FVoxelLandM4ErosionSettings Erosion;
	TArray<int32> Input = {100, 60, -20, -40};
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	uint64 Hash = 0;
	FVoxelLandError Error =
		VoxelLandM4Prepass::Generate(Domain, Input, 42, FGuid(), 0, 0, Erosion, Grid, Hash, [] { return false; });
	if (!TestFalse(TEXT("prepass succeeds"), bool(Error)) || !TestTrue(TEXT("snapshot created"), Grid.IsValid()))
	{
		return false;
	}
	int32 Height = 0;
	TestTrue(TEXT("negative origin samples"), Grid->Sample(-100, -100, Height));
	TestEqual(TEXT("row-major first height"), Height, 100);
	TestTrue(TEXT("last inclusive sample"), Grid->Sample(0, 0, Height));
	TestEqual(TEXT("row-major last height"), Height, -40);
	Input[0] = 12;
	TestTrue(TEXT("snapshot survives input mutation"), Grid->Sample(-100, -100, Height));
	TestEqual(TEXT("original height retained"), Height, 100);
	const uint64 SavedHash = Hash;
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Original = Grid;
	Error = VoxelLandM4Prepass::Generate(Domain, Input, 42, FGuid(), 0, 0, Erosion, Grid, Hash, [] { return true; });
	TestTrue(TEXT("cancellation propagated"), Error.Code == EVoxelLandErrorCode::Cancelled);
	TestTrue(TEXT("cancelled snapshot unchanged"), Grid == Original);
	TestEqual(TEXT("cancelled identity unchanged"), Hash, SavedHash);
	int32 Checks = 0;
	Error = VoxelLandM4Prepass::Generate(
		Domain, Input, 42, FGuid(), 0, 0, Erosion, Grid, Hash, [&Checks] { return ++Checks == 5; });
	TestTrue(TEXT("cancellation during grid creation propagated"), Error.Code == EVoxelLandErrorCode::Cancelled);
	TestTrue(TEXT("cancelled grid construction leaves snapshot unchanged"), Grid == Original);
	TestEqual(TEXT("cancelled grid construction leaves identity unchanged"), Hash, SavedHash);
	Input.Pop();
	Error = VoxelLandM4Prepass::Generate(Domain, Input, 42, FGuid(), 0, 0, Erosion, Grid, Hash, [] { return false; });
	TestTrue(TEXT("invalid grid rejected"), bool(Error));
	TestTrue(TEXT("invalid grid leaves previous snapshot"), Grid == Original);
	TestEqual(TEXT("invalid grid leaves identity"), Hash, SavedHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4PrepassErosionTest, "VoxelLand.M4.Prepass.Erosion", M4PrepassTestFlags)
bool FVoxelLandM4PrepassErosionTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4DomainSettings Domain;
	Domain.CellsX = 1;
	Domain.CellsY = 1;
	Domain.OriginXCm = -100;
	Domain.OriginYCm = -100;
	Domain.GridSpacingCm = 100;
	FVoxelLandM4ErosionSettings Erosion;
	Erosion.ThermalIterations = 1;
	const TArray<int32> Input = {100, 0, 0, 0};
	const FGuid Stage(1, 2, 3, 4);
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	uint64 Hash = 0;
	FVoxelLandError Error =
		VoxelLandM4Prepass::Generate(Domain, Input, 42, Stage, 7, 0, Erosion, Grid, Hash, [] { return false; });
	if (!TestFalse(TEXT("finite erosion succeeds"), bool(Error)) || !TestTrue(TEXT("grid published"), Grid.IsValid()))
	{
		return false;
	}
	int32 Height = 0;
	TestTrue(TEXT("source sample succeeds"), Grid->Sample(-100, -100, Height));
	TestEqual(TEXT("thermal transfer applied before publication"), Height, 75);
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Repeat;
	uint64 RepeatHash = 0;
	Error =
		VoxelLandM4Prepass::Generate(Domain, Input, 42, Stage, 7, 0, Erosion, Repeat, RepeatHash, [] { return false; });
	TestFalse(TEXT("repeat succeeds"), bool(Error));
	TestEqual(TEXT("same inputs produce same result identity"), RepeatHash, Hash);
	Domain.Mode = EVoxelLandM4DomainMode::Unlimited;
	Domain.bGlobalErosion = false;
	Error =
		VoxelLandM4Prepass::Generate(Domain, Input, 42, Stage, 7, 0, Erosion, Repeat, RepeatHash, [] { return false; });
	TestTrue(TEXT("unlimited mode cannot run a global prepass"), bool(Error));
	TestEqual(TEXT("failure preserves previous identity"), RepeatHash, Hash);
	return true;
}

#endif
