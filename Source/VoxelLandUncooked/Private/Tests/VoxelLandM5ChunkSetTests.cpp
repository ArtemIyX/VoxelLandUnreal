#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Structs/VoxelLandChunkData.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5ChunkSet.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5ChunkSetFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

int32 M5ChunkSetTestIndex(int32 X, int32 Y, int32 Z)
{
	return X + 35 * (Y + 35 * Z);
}

TArray<FVoxelLandChunkData> M5ChunkSetFixture()
{
	TArray<FVoxelLandChunkData> Chunks;
	for (int32 Z = -3; Z < -1; ++Z)
	{
		for (int32 Y = -3; Y < -1; ++Y)
		{
			for (int32 X = -3; X < -1; ++X)
			{
				FVoxelLandChunkData& C = Chunks.AddDefaulted_GetRef();
				C.Key.Coordinate = FIntVector(X, Y, Z);
				C.Density.SetNumUninitialized(FVoxelLandChunkData::SampleCount);
				for (int32 K = 0; K < 35; ++K)
				{
					for (int32 J = 0; J < 35; ++J)
					{
						for (int32 I = 0; I < 35; ++I)
						{
							// Independent global affine field, including all halo samples.
							C.Density[M5ChunkSetTestIndex(I, J, K)] =
								int16((X * 32 + I - 1) + 3 * (Y * 32 + J - 1) + 5 * (Z * 32 + K - 1));
						}
					}
				}
			}
		}
	}
	return Chunks;
}

TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> M5ChunkSetBudget(int64 Work)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	FVoxelLandM5Budget::TryCreate(0, Work, Budget);
	return Budget;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5ChunkSetSeamsTest, "VoxelLand.M5.ChunkSet.ThreeAxisHalos", M5ChunkSetFlags)
bool FVoxelLandM5ChunkSetSeamsTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::ChunkSet;
	const auto NeverCancel = []() {
		return false;
	};
	TArray<FVoxelLandChunkData> Chunks = M5ChunkSetFixture();
	const FIntVector Min(-3, -3, -3), Max(-1, -1, -1);
	const auto Check = [&]() {
		auto Budget = M5ChunkSetBudget(1000000);
		return CheckSampleAgreement(Chunks, Min, Max, 8, Budget.ToSharedRef(), NeverCancel).Code;
	};
	TestEqual(TEXT("all negative-coordinate neighbor halos match"), int32(Check()), int32(EVoxelLandM5ErrorCode::None));
	Chunks.Swap(0, 7);
	TestEqual(TEXT("input order does not matter"), int32(Check()), int32(EVoxelLandM5ErrorCode::None));
	Chunks.Swap(0, 7);
	// The three values occupy the upper chunk's farthest overlap plane, for
	// each independent axis. The expected mismatch is not obtained from production.
	const int32 Indices[] = {
		M5ChunkSetTestIndex(34, 10, 11), M5ChunkSetTestIndex(10, 34, 11), M5ChunkSetTestIndex(10, 11, 34)};
	for (int32 Index : Indices)
	{
		const int16 Original = Chunks[0].Density[Index];
		Chunks[0].Density[Index] = int16(Original + 1);
		TestEqual(TEXT("changed shared plane rejected"), int32(Check()), int32(EVoxelLandM5ErrorCode::Unproven));
		Chunks[0].Density[Index] = Original;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5ChunkSetFailureTest, "VoxelLand.M5.ChunkSet.Failures", M5ChunkSetFlags)
bool FVoxelLandM5ChunkSetFailureTest::RunTest(const FString& Parameters)
{
	using namespace VoxelLand::M5::ChunkSet;
	const auto NeverCancel = []() {
		return false;
	};
	const FIntVector Min(-3, -3, -3), Max(-1, -1, -1);
	TArray<FVoxelLandChunkData> Chunks = M5ChunkSetFixture();
	auto Check = [&](int64 Work) {
		auto Budget = M5ChunkSetBudget(Work);
		return CheckSampleAgreement(Chunks, Min, Max, 8, Budget.ToSharedRef(), NeverCancel).Code;
	};
	// 4*8^2 + 8*12000 + 1 is the documented conservative precharge.
	TestEqual(
		TEXT("work just below admission fails"), int32(Check(96256)), int32(EVoxelLandM5ErrorCode::BudgetExceeded));
	TestEqual(TEXT("work at admission succeeds"), int32(Check(96257)), int32(EVoxelLandM5ErrorCode::None));
	Chunks.Pop();
	TestEqual(TEXT("missing chunk rejected"), int32(Check(1000000)), int32(EVoxelLandM5ErrorCode::Unproven));
	Chunks = M5ChunkSetFixture();
	Chunks[7].Key.Coordinate = Chunks[0].Key.Coordinate;
	TestEqual(TEXT("duplicate chunk rejected"), int32(Check(1000000)), int32(EVoxelLandM5ErrorCode::InvalidInput));
	Chunks = M5ChunkSetFixture();
	Chunks[2].Density.Pop();
	TestEqual(TEXT("missing sample rejected"), int32(Check(1000000)), int32(EVoxelLandM5ErrorCode::InvalidInput));
	Chunks = M5ChunkSetFixture();
	Chunks[2].Key.LOD = 1;
	TestEqual(TEXT("nonzero LOD rejected"), int32(Check(1000000)), int32(EVoxelLandM5ErrorCode::InvalidInput));
	Chunks = M5ChunkSetFixture();
	int32 Calls = 0;
	const auto CancelDuringCheck = [&Calls]() {
		return ++Calls >= 20;
	};
	auto Budget = M5ChunkSetBudget(1000000);
	const int16 Original = Chunks[0].Density[0];
	TestEqual(TEXT("bounded cancellation"),
		int32(CheckSampleAgreement(Chunks, Min, Max, 8, Budget.ToSharedRef(), CancelDuringCheck).Code),
		int32(EVoxelLandM5ErrorCode::Cancelled));
	TestEqual(TEXT("cancellation does not alter samples"), Chunks[0].Density[0], Original);
	return true;
}
#endif
