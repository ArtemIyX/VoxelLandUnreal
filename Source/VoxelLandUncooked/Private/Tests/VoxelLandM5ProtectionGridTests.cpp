#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5ProtectionGrid.h"
#include "Generation/VoxelLandM5ProtectionSamples.h"
#include "Misc/AutomationTest.h"

class FVoxelLandM5ProtectionGridTestHelpers final
{
public:
	static FVoxelLandM5PointCm Point(int64 X, int64 Y, int64 Z)
	{
		FVoxelLandM5PointCm Result;
		Result.X = X;
		Result.Y = Y;
		Result.Z = Z;
		return Result;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5ProtectionGridTest, "VoxelLand.M5.Protection.IncidentCells",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM5ProtectionGridTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget"), FVoxelLandM5Budget::TryCreate(100000, 100000, Budget)))
	{
		return false;
	}
	const auto Never = [] { return false; };
	const auto Always = [] { return true; };
	TArray<uint8> Cells;
	Cells.Init(0, 64);
	Cells[1 + 4 * (1 + 4)] = 1;
	TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> Grid;
	FVoxelLandM5Diagnostic D = FVoxelLandM5ProtectionGrid::Capture(FIntVector(4, 4, 4),
		FVoxelLandM5ProtectionGridTestHelpers::Point(-100, -100, -100), 10, Cells, Budget.ToSharedRef(), Never, Grid);
	if (!TestEqual(TEXT("capture"), D.Code, EVoxelLandM5ErrorCode::None) || !TestTrue(TEXT("grid"), Grid.IsValid()))
	{
		return false;
	}
	// Capture owns its copy, not the caller's mutable array.
	Cells[1 + 4 * (1 + 4)] = 0;
	TSharedPtr<const FVoxelLandM5ProtectionSamples, ESPMode::ThreadSafe> Samples;
	D = Grid->Project(FIntVector(3, 3, 3), FVoxelLandM5ProtectionGridTestHelpers::Point(-90, -90, -90),
		Budget.ToSharedRef(), Never, Samples);
	if (!TestEqual(TEXT("project"), D.Code, EVoxelLandM5ErrorCode::None) || !TestTrue(TEXT("samples"), Samples.IsValid()))
	{
		return false;
	}
	for (int32 Z = 0; Z < 3; ++Z)
	{
		for (int32 Y = 0; Y < 3; ++Y)
		{
			for (int32 X = 0; X < 3; ++X)
			{
				const uint8 Expected = X < 2 && Y < 2 && Z < 2 ? 1 : 0;
				TestEqual(TEXT("incident corners and X-fastest layout"), Samples->GetSamples()[X + 3 * (Y + 3 * Z)], Expected);
			}
		}
	}
	const auto Saved = Samples;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> OtherBudget;
	if (!TestTrue(TEXT("other budget"), FVoxelLandM5Budget::TryCreate(100000, 100000, OtherBudget)))
	{
		return false;
	}
	D = Grid->Project(FIntVector(1, 1, 1), FVoxelLandM5ProtectionGridTestHelpers::Point(-90, -90, -90),
		OtherBudget.ToSharedRef(), Never, Samples);
	TestEqual(TEXT("foreign budget rejected"), D.Code, EVoxelLandM5ErrorCode::InvalidInput);
	TestTrue(TEXT("foreign budget preserves samples"), Samples == Saved);
	D = Grid->Project(FIntVector(1, 1, 1), FVoxelLandM5ProtectionGridTestHelpers::Point(-100, -90, -90),
		Budget.ToSharedRef(), Never, Samples);
	TestEqual(TEXT("missing negative halo"), D.Code, EVoxelLandM5ErrorCode::OutOfDomain);
	TestTrue(TEXT("failure preserves samples"), Samples == Saved);
	D = Grid->Project(FIntVector(1, 1, 1), FVoxelLandM5ProtectionGridTestHelpers::Point(-60, -90, -90),
		Budget.ToSharedRef(), Never, Samples);
	TestEqual(TEXT("missing positive halo"), D.Code, EVoxelLandM5ErrorCode::OutOfDomain);
	D = Grid->Project(FIntVector(1, 1, 1), FVoxelLandM5ProtectionGridTestHelpers::Point(-89, -90, -90),
		Budget.ToSharedRef(), Never, Samples);
	TestEqual(TEXT("unaligned"), D.Code, EVoxelLandM5ErrorCode::InvalidInput);
	D = Grid->Project(FIntVector(1, 1, 1), FVoxelLandM5ProtectionGridTestHelpers::Point(-90, -90, -90),
		Budget.ToSharedRef(), Always, Samples);
	TestEqual(TEXT("projection cancellation"), D.Code, EVoxelLandM5ErrorCode::Cancelled);
	TestTrue(TEXT("cancel preserves samples"), Samples == Saved);
	const auto SavedGrid = Grid;
	Cells[0] = 2;
	D = FVoxelLandM5ProtectionGrid::Capture(FIntVector(4, 4, 4),
		FVoxelLandM5ProtectionGridTestHelpers::Point(-100, -100, -100), 10, Cells, Budget.ToSharedRef(), Never, Grid);
	TestEqual(TEXT("nonbinary rejected"), D.Code, EVoxelLandM5ErrorCode::InvalidInput);
	TestTrue(TEXT("invalid capture preserves grid"), Grid == SavedGrid);
	D = FVoxelLandM5ProtectionGrid::Capture(FIntVector(4, 4, 4),
		FVoxelLandM5ProtectionGridTestHelpers::Point(MAX_int64, 0, 0), 10, Cells, Budget.ToSharedRef(), Never, Grid);
	TestEqual(TEXT("extent overflow"), D.Code, EVoxelLandM5ErrorCode::ArithmeticOverflow);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5ProtectionColumnsTest, "VoxelLand.M5.Protection.Columns",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM5ProtectionColumnsTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget"), FVoxelLandM5Budget::TryCreate(100000, 100000, Budget)))
	{
		return false;
	}
	const auto Never = [] { return false; };
	TArray<uint8> Cells;
	Cells.Init(0, 64);
	// Last Z plane proves that all captured layers are considered.
	Cells[1 + 4 * (1 + 4 * 3)] = 1;
	TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> Grid;
	const auto Origin = FVoxelLandM5ProtectionGridTestHelpers::Point(-100, -100, -100);
	FVoxelLandM5Diagnostic D = FVoxelLandM5ProtectionGrid::Capture(
		FIntVector(4, 4, 4), Origin, 10, Cells, Budget.ToSharedRef(), Never, Grid);
	if (!TestEqual(TEXT("capture"), D.Code, EVoxelLandM5ErrorCode::None) || !Grid.IsValid())
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5ProtectionSamples, ESPMode::ThreadSafe> Columns;
	const auto ColumnOrigin = FVoxelLandM5ProtectionGridTestHelpers::Point(-90, -90, MAX_int64);
	D = Grid->ProjectColumns(FIntPoint(3, 3), ColumnOrigin, Budget.ToSharedRef(), Never, Columns);
	if (!TestEqual(TEXT("column projection"), D.Code, EVoxelLandM5ErrorCode::None) || !Columns.IsValid())
	{
		return false;
	}
	for (int32 Y = 0; Y < 3; ++Y)
	{
		for (int32 X = 0; X < 3; ++X)
		{
			TestEqual(TEXT("incident XY corners and X-fastest layout"),
				Columns->GetSamples()[X + 3 * Y], uint8(X < 2 && Y < 2 ? 1 : 0));
		}
	}
	const auto Saved = Columns;
	D = Grid->ProjectColumns(FIntPoint(1, 1), Origin, Budget.ToSharedRef(), Never, Columns);
	TestEqual(TEXT("missing negative XY halo"), D.Code, EVoxelLandM5ErrorCode::OutOfDomain);
	TestTrue(TEXT("missing halo preserves columns"), Columns == Saved);
	D = Grid->ProjectColumns(FIntPoint(1, 1), FVoxelLandM5ProtectionGridTestHelpers::Point(-60, -90, 0),
		Budget.ToSharedRef(), Never, Columns);
	TestEqual(TEXT("missing positive XY halo"), D.Code, EVoxelLandM5ErrorCode::OutOfDomain);
	D = Grid->ProjectColumns(FIntPoint(1, 1), FVoxelLandM5ProtectionGridTestHelpers::Point(-89, -90, 0),
		Budget.ToSharedRef(), Never, Columns);
	TestEqual(TEXT("unaligned column"), D.Code, EVoxelLandM5ErrorCode::InvalidInput);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> OtherBudget;
	if (!TestTrue(TEXT("other budget"), FVoxelLandM5Budget::TryCreate(100000, 100000, OtherBudget)))
	{
		return false;
	}
	D = Grid->ProjectColumns(FIntPoint(1, 1), ColumnOrigin, OtherBudget.ToSharedRef(), Never, Columns);
	TestEqual(TEXT("foreign budget"), D.Code, EVoxelLandM5ErrorCode::InvalidInput);
	int32 Polls = 0;
	const auto CancelDuring = [&Polls] { return ++Polls >= 2; };
	D = Grid->ProjectColumns(FIntPoint(1, 1), ColumnOrigin, Budget.ToSharedRef(), CancelDuring, Columns);
	TestEqual(TEXT("in-flight cancellation"), D.Code, EVoxelLandM5ErrorCode::Cancelled);
	TestTrue(TEXT("failed calls preserve columns"), Columns == Saved);
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> TightBudget;
	if (!TestTrue(TEXT("tight budget"), FVoxelLandM5Budget::TryCreate(100000, 150, TightBudget)))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> TightGrid;
	D = FVoxelLandM5ProtectionGrid::Capture(FIntVector(4, 4, 4), Origin, 10, Cells,
		TightBudget.ToSharedRef(), Never, TightGrid);
	if (!TestEqual(TEXT("tight capture"), D.Code, EVoxelLandM5ErrorCode::None) || !TightGrid.IsValid())
	{
		return false;
	}
	D = TightGrid->ProjectColumns(FIntPoint(2, 2), ColumnOrigin, TightBudget.ToSharedRef(), Never, Columns);
	TestEqual(TEXT("column work budget"), D.Code, EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("budget failure preserves columns"), Columns == Saved);
	return true;
}
#endif
