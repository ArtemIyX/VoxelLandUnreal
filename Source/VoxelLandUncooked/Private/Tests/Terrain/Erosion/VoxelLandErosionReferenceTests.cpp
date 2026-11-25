#if WITH_DEV_AUTOMATION_TESTS

#include "Generation/Terrain/Erosion/VoxelLandErosion.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M4ErosionReferenceFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

struct FReferenceErosionResult
{
	TArray<int64> Heights;
	int32 Removed = 0;
	int32 Deposited = 0;
	int32 TerminalDeposits = 0;
	int32 ThermalTransfers = 0;
};

// Test-only scalar model. All elevations are int64, and thermal updates use a
// separate next grid so the oracle never observes another cell's transfer.
FReferenceErosionResult ReferenceErosion(int32 Width, int32 Height, const TArray<int32>& Initial,
	const TArray<FIntPoint>& Starts, const FVoxelLandM4ErosionSettings& Settings)
{
	FReferenceErosionResult Result;
	for (int32 Value : Initial)
	{
		Result.Heights.Add(Value);
	}
	auto Lowest = [Width, Height](const TArray<int64>& Grid, FIntPoint Position) {
		FIntPoint Best = Position;
		const FIntPoint Neighbors[] = {{Position.X + 1, Position.Y}, {Position.X - 1, Position.Y},
			{Position.X, Position.Y + 1}, {Position.X, Position.Y - 1}};
		for (const FIntPoint& Neighbor : Neighbors)
		{
			if (Neighbor.X >= 0 && Neighbor.X < Width && Neighbor.Y >= 0 && Neighbor.Y < Height
				&& Grid[Neighbor.Y * Width + Neighbor.X] < Grid[Best.Y * Width + Best.X])
			{
				Best = Neighbor;
			}
		}
		return Best;
	};
	for (FIntPoint Position : Starts)
	{
		int64 Sediment = 0;
		for (int32 Step = 0; Step < Settings.MaxSteps; ++Step)
		{
			const FIntPoint Next = Lowest(Result.Heights, Position);
			if (Next == Position)
			{
				break;
			}
			const int32 CurrentIndex = Position.Y * Width + Position.X;
			const int64 Drop = Result.Heights[CurrentIndex] - Result.Heights[Next.Y * Width + Next.X];
			const int64 Capacity = FMath::Min(Settings.CapacityMaxQ8, Drop / 4);
			if (Sediment > Capacity)
			{
				const int64 Amount = (Sediment - Capacity) / 2;
				Result.Heights[CurrentIndex] += Amount;
				Sediment -= Amount;
				Result.Deposited += Amount > 0;
			}
			else
			{
				const int64 Amount = FMath::Min3((Capacity - Sediment) / 2, Settings.MaxTransferQ8, Drop / 4);
				Result.Heights[CurrentIndex] -= Amount;
				Sediment += Amount;
				Result.Removed += Amount > 0;
			}
			Position = Next;
		}
		Result.Heights[Position.Y * Width + Position.X] += Sediment;
		Result.TerminalDeposits += Sediment > 0;
	}
	for (int32 Iteration = 0; Iteration < Settings.ThermalIterations; ++Iteration)
	{
		TArray<int64> NextGrid = Result.Heights;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const FIntPoint Next = Lowest(Result.Heights, FIntPoint(X, Y));
				const int32 From = Y * Width + X, To = Next.Y * Width + Next.X;
				const int64 Difference = Result.Heights[From] - Result.Heights[To];
				if (Difference <= Settings.TalusQ8)
				{
					continue;
				}
				const int64 Amount = FMath::Min(Settings.MaxTransferQ8, (Difference - Settings.TalusQ8) / 4);
				NextGrid[From] -= Amount;
				NextGrid[To] += Amount;
				Result.ThermalTransfers += Amount > 0;
			}
		}
		Result.Heights = MoveTemp(NextGrid);
	}
	return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4ErosionReferenceTest, "VoxelLand.M4.Erosion.IndependentReference", M4ErosionReferenceFlags)
bool FVoxelLandM4ErosionReferenceTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4ErosionSettings Settings;
	Settings.MaxSteps = 3;
	Settings.ThermalIterations = 2;
	Settings.TalusQ8 = 3;
	Settings.CapacityMaxQ8 = 80;
	Settings.MaxTransferQ8 = 13;
	const TArray<int32> Initial = {100, 60, 59, 170, 100, 100, 30, 90, -40, 10, 70, 50};
	const TArray<FIntPoint> Starts = {{0, 0}, {3, 0}, {3, 2}, {0, 2}, {1, 1}};
	const FReferenceErosionResult Expected = ReferenceErosion(4, 3, Initial, Starts, Settings);
	TestTrue(TEXT("reference exercises erosion"), Expected.Removed > 0);
	TestTrue(TEXT("reference exercises deposits"), Expected.Deposited > 0);
	TestTrue(TEXT("reference exercises terminal sediment"), Expected.TerminalDeposits > 0);
	TestTrue(TEXT("reference exercises thermal transfer"), Expected.ThermalTransfers > 0);
	TArray<int32> Actual;
	const FVoxelLandError Error =
		VoxelLandM4Erosion::Apply(3, 2, Initial, Starts, Settings, Actual, [] { return false; });
	if (!TestFalse(TEXT("combined erosion succeeds"), bool(Error))
		|| !TestEqual(TEXT("sample count"), Actual.Num(), Initial.Num()))
	{
		return false;
	}
	int64 Before = 0, After = 0;
	for (int32 Index = 0; Index < Initial.Num(); ++Index)
	{
		Before += Initial[Index];
		After += Actual[Index];
		TestEqual(FString::Printf(TEXT("reference sample %d"), Index), int64(Actual[Index]), Expected.Heights[Index]);
	}
	TestEqual(TEXT("combined passes conserve height"), After, Before);

	// Bad coordinates must not publish a partly transformed grid.
	TArray<int32> Unchanged = {41, 42};
	const TArray<FIntPoint> InvalidStarts = {{0, 0}, {4, 2}};
	const FVoxelLandError Invalid =
		VoxelLandM4Erosion::Apply(3, 2, Initial, InvalidStarts, Settings, Unchanged, [] { return false; });
	TestTrue(TEXT("invalid boundary rejected"), Invalid.Code == EVoxelLandErrorCode::InvalidParameter);
	TestTrue(TEXT("rejected erosion leaves output untouched"), Unchanged == TArray<int32>({41, 42}));
	TArray<int32> OutsideRange = Initial;
	OutsideRange[0] = 512000001;
	const FVoxelLandError Overflow =
		VoxelLandM4Erosion::Apply(3, 2, OutsideRange, Starts, Settings, Unchanged, [] { return false; });
	TestTrue(TEXT("out-of-range elevation rejected"), Overflow.Code == EVoxelLandErrorCode::InvalidParameter);
	TestTrue(TEXT("range failure leaves output untouched"), Unchanged == TArray<int32>({41, 42}));
	return true;
}
#endif
