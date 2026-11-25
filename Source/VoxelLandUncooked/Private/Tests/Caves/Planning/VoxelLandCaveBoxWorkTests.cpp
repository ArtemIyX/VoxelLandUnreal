#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Terrain/Shaping/VoxelLandTerrainSmoothingSettings.h"
#include "Data/Terrain/Shaping/VoxelLandHeightDetailConfig.h"
#include "Data/Caves/Geometry/VoxelLandCavePointCm.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightGrid.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5BoxWorkTest, "VoxelLand.M5.BoxWork.TileAccounting",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5BoxWorkTest::RunTest(const FString& Parameters)
{
	bool bExact = false, bBelow = false, bSmooth = false, bCancel = false, bBounds = false;
	bool bDetailExact = false, bDetailBelow = false;
	const bool bSetup = Async(EAsyncExecution::ThreadPool, [&]() {
		FVoxelLandProgramDraft Draft;
		Draft.RegisterCount = 1;
		FVoxelLandInstruction Input, Store;
		Input.Op = EVoxelLandOpcode::LoadZ;
		Store.Op = EVoxelLandOpcode::StoreDensity;
		Draft.Instructions = {Input, Store};
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (FVoxelLandProgram::Validate(Draft, Program))
		{
			return false;
		}
		TArray<int32> Heights;
		Heights.Init(0, 101 * 101);
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
		if (!FVoxelLandM4HeightGrid::Create(-5000, -5000, 100, 100, 100, Heights, Grid))
		{
			return false;
		}
		FVoxelLandGeneratorSettings Settings;
		Settings.Program = Program;
		Settings.Profile.VoxelSizeCm = 100.f;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Plain, Smooth;
		FVoxelLandM4SmoothSettings Filter;
		Filter.bEnabled = true;
		Filter.Strength = 0.5f;
		FVoxelLandM4OverhangConfig Overhang;
		if (FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, false, Plain)
			|| FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, false, Overhang, Filter, Smooth) || !Plain
			|| !Smooth)
		{
			return false;
		}
		TUniquePtr<FVoxelLandWorkerContext> PlainWorker, SmoothWorker;
		if (Plain->CreateWorker(PlainWorker) || Smooth->CreateWorker(SmoothWorker) || !PlainWorker || !SmoothWorker)
		{
			return false;
		}
		FVoxelLandM5PointCm Min;
		Min.X = -36;
		Min.Y = Min.Z = -4;
		const FIntVector Size(73, 41, 41);
		constexpr int64 Count = 73ll * 41 * 41;
		// Three X tiles and two Y/Z tiles. Smooth halos repeat at every tile,
		// including overlaps: (73+6)*(41+4)*(41+4) raw samples.
		constexpr int64 PlainWork = Count * 8 + Count * 2;
		constexpr int64 SmoothWork = 79ll * 45 * 45 * 8 + Count * 30;
		auto BudgetFor = [](int64 Limit) {
			TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
			FVoxelLandM5Budget::TryCreate(16 * 1024 * 1024, Limit, Budget);
			return Budget;
		};
		auto Exact = BudgetFor(PlainWork);
		TArray<int16> Density;
		const auto Result =
			Plain->GenerateFiniteM4Box(Min, Size, *PlainWorker, Exact.ToSharedRef(), [] { return false; }, Density);
		int64 Live = 0, Peak = 0, Used = 0;
		Exact->GetUsage(Live, Peak, Used);
		bExact = Result.Code == EVoxelLandM5ErrorCode::None && Density.Num() == Count && Used == PlainWork;
		auto Below = BudgetFor(PlainWork - 1);
		TArray<int16> Previous = {123};
		const auto Rejected =
			Plain->GenerateFiniteM4Box(Min, Size, *PlainWorker, Below.ToSharedRef(), [] { return false; }, Previous);
		bBelow = Rejected.Code == EVoxelLandM5ErrorCode::BudgetExceeded && Previous.Num() == 1 && Previous[0] == 123;
		auto SmoothBudget = BudgetFor(SmoothWork);
		TArray<int16> Smoothed;
		const auto Filtered = Smooth->GenerateFiniteM4Box(
			Min, Size, *SmoothWorker, SmoothBudget.ToSharedRef(), [] { return false; }, Smoothed);
		SmoothBudget->GetUsage(Live, Peak, Used);
		bSmooth = Filtered.Code == EVoxelLandM5ErrorCode::None && Smoothed.Num() == Count && Used == SmoothWork;
		FVoxelLandM4HeightDetailConfig Detail;
		Detail.bEnabled = true;
		Detail.AmplitudeQ8 = 128000;
		Detail.FrequencyPerCm = 0.004f;
		Detail.NodeId = FGuid(11, 22, 33, 44);
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Detailed;
		TUniquePtr<FVoxelLandWorkerContext> DetailedWorker;
		if (FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, true, Overhang, Detail, Detailed) || !Detailed
			|| Detailed->CreateWorker(DetailedWorker) || !DetailedWorker)
		{
			return false;
		}
		// 36³ gives 35+1 on all axes. Native height detail uses a one-cell
		// slope stencil per XY tile and is recomputed for both Z tiles:
		// ((35+2)+(1+2))² * 2 = 3200 detail samples.
		const FIntVector DetailSize(36, 36, 36);
		constexpr int64 DetailCount = 36ll * 36 * 36;
		constexpr int64 DetailWork = DetailCount * (8 + 2) + 40ll * 40 * 2 * 8;
		auto DetailBudget = BudgetFor(DetailWork);
		TArray<int16> DetailOutput;
		const auto DetailResult = Detailed->GenerateFiniteM4Box(
			Min, DetailSize, *DetailedWorker, DetailBudget.ToSharedRef(), [] { return false; }, DetailOutput);
		DetailBudget->GetUsage(Live, Peak, Used);
		bDetailExact =
			DetailResult.Code == EVoxelLandM5ErrorCode::None && DetailOutput.Num() == DetailCount && Used == DetailWork;
		auto DetailTiny = BudgetFor(DetailWork - 1);
		TArray<int16> DetailPrior = {42};
		const auto DetailReject = Detailed->GenerateFiniteM4Box(
			Min, DetailSize, *DetailedWorker, DetailTiny.ToSharedRef(), [] { return false; }, DetailPrior);
		bDetailBelow = DetailReject.Code == EVoxelLandM5ErrorCode::BudgetExceeded && DetailPrior.Num() == 1
					&& DetailPrior[0] == 42;
		auto CancelBudget = BudgetFor(PlainWork);
		TArray<int16> Prior = {77};
		const auto CancelAfterCharge = [&CancelBudget]() {
			int64 A = 0, B = 0, C = 0;
			CancelBudget->GetUsage(A, B, C);
			return C > 0;
		};
		const auto Cancelled =
			Plain->GenerateFiniteM4Box(Min, Size, *PlainWorker, CancelBudget.ToSharedRef(), CancelAfterCharge, Prior);
		bCancel = Cancelled.Code == EVoxelLandM5ErrorCode::Cancelled && Prior.Num() == 1 && Prior[0] == 77;
		FVoxelLandM5PointCm Extreme = Min;
		Extreme.X = MAX_int64;
		const auto Invalid = Plain->GenerateFiniteM4Box(
			Extreme, Size, *PlainWorker, CancelBudget.ToSharedRef(), [] { return false; }, Prior);
		bBounds = Invalid.Code == EVoxelLandM5ErrorCode::OutOfDomain && Prior.Num() == 1 && Prior[0] == 77;
		return true;
	}).Get();
	TestTrue(TEXT("fixture setup"), bSetup);
	TestTrue(TEXT("unsmoothed actual tiles fit exact work"), bExact);
	TestTrue(TEXT("one below work preserves output"), bBelow);
	TestTrue(TEXT("smooth overlap tiles charged exactly"), bSmooth);
	TestTrue(TEXT("native detail and slope thin tiles fit exact work"), bDetailExact);
	TestTrue(TEXT("native detail one below work preserves output"), bDetailBelow);
	TestTrue(TEXT("cancellation after admission preserves output"), bCancel);
	TestTrue(TEXT("overflowing lattice bounds preserve output"), bBounds);
	return bSetup;
}
#endif
