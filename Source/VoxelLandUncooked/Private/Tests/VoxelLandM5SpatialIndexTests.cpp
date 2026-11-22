#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5SpatialIndex.h"
#include "Misc/AutomationTest.h"

#include <limits>

namespace
{
constexpr EAutomationTestFlags M5SpatialTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandM5InfluenceBounds M5TestBounds(uint64 Id, FVector3d Min, FVector3d Max)
{
	FVoxelLandM5InfluenceBounds Entry;
	Entry.PrimitiveId = Id;
	Entry.Bounds = FBox3d(Min, Max);
	return Entry;
}

// Independent separating-axis oracle, deliberately structured as rejection
// instead of the production conjunction of inclusive overlap comparisons.
bool M5ReferenceIntersects(const FBox3d& A, const FBox3d& B)
{
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (A.Max[Axis] < B.Min[Axis] || B.Max[Axis] < A.Min[Axis])
		{
			return false;
		}
	}
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5SpatialIndexReferenceTest, "VoxelLand.M5.Spatial.Reference", M5SpatialTestFlags)
bool FVoxelLandM5SpatialIndexReferenceTest::RunTest(const FString& Parameters)
{
	TArray<FVoxelLandM5InfluenceBounds> Entries;
	Entries.Add(M5TestBounds(42, FVector3d(-2, -2, -2), FVector3d(0, 0, 0)));
	Entries.Add(M5TestBounds(1, FVector3d(0, 0, 0), FVector3d(0, 0, 0)));
	Entries.Add(M5TestBounds(99, FVector3d(0, -1, -1), FVector3d(3, 1, 1)));
	Entries.Add(M5TestBounds(8, FVector3d(3, 1, 1), FVector3d(4, 2, 2)));
	Entries.Add(M5TestBounds(7, FVector3d(8, 8, 8), FVector3d(9, 9, 9)));
	const auto NeverCancel = []() {
		return false;
	};
	TSharedPtr<const FVoxelLandM5SpatialIndex, ESPMode::ThreadSafe> Index;
	TestFalse(TEXT("build succeeds"), bool(FVoxelLandM5SpatialIndex::Build(Entries, 5, NeverCancel, Index)));
	if (!TestTrue(TEXT("index was published"), Index.IsValid()))
	{
		return false;
	}
	TArray<uint64> PointResult;
	const FBox3d Origin(FVector3d(0, 0, 0), FVector3d(0, 0, 0));
	TestFalse(TEXT("point query succeeds"), bool(Index->Query(Origin, NeverCancel, PointResult)));
	TestTrue(TEXT("point contact includes faces and degenerate boxes in sorted ID order"),
		PointResult == TArray<uint64>({1, 42, 99}));

	// Different translated volumes, including degenerate extents and negative
	// coordinates, are checked against an independently written exhaustive oracle.
	for (int32 X = -4; X <= 10; ++X)
	{
		for (int32 Y = -3; Y <= 3; ++Y)
		{
			const FBox3d Query(FVector3d(X, Y, -1), FVector3d(X + 1, Y, 1));
			TArray<uint64> Expected;
			for (const FVoxelLandM5InfluenceBounds& Entry : Entries)
			{
				if (M5ReferenceIntersects(Entry.Bounds, Query))
				{
					Expected.Add(Entry.PrimitiveId);
				}
			}
			Expected.Sort();
			TArray<uint64> Actual = {12345};
			const FVoxelLandError Error = Index->Query(Query, NeverCancel, Actual);
			if (Error || Actual != Expected)
			{
				AddError(FString::Printf(TEXT("oracle mismatch for x=%d y=%d"), X, Y));
				return false;
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5SpatialIndexFailureTest, "VoxelLand.M5.Spatial.FailureAtomicity", M5SpatialTestFlags)
bool FVoxelLandM5SpatialIndexFailureTest::RunTest(const FString& Parameters)
{
	const auto NeverCancel = []() {
		return false;
	};
	const auto AlwaysCancel = []() {
		return true;
	};
	TArray<FVoxelLandM5InfluenceBounds> Entries = {M5TestBounds(1, FVector3d(0, 0, 0), FVector3d(1, 1, 1)),
		M5TestBounds(2, FVector3d(1, 1, 1), FVector3d(2, 2, 2))};
	TSharedPtr<const FVoxelLandM5SpatialIndex, ESPMode::ThreadSafe> Index;
	TestFalse(TEXT("initial build"), bool(FVoxelLandM5SpatialIndex::Build(Entries, 2, NeverCancel, Index)));
	if (!TestTrue(TEXT("initial index valid"), Index.IsValid()))
	{
		return false;
	}
	const FVoxelLandM5SpatialIndex* Original = Index.Get();
	const auto ExpectBuildFailure = [this, &Index, Original](const TCHAR* Label, const FVoxelLandError& Error) {
		TestTrue(Label, bool(Error));
		TestTrue(TEXT("failed build retains existing index"), Index.Get() == Original);
	};
	ExpectBuildFailure(TEXT("entry budget"), FVoxelLandM5SpatialIndex::Build(Entries, 1, NeverCancel, Index));
	ExpectBuildFailure(TEXT("negative budget"), FVoxelLandM5SpatialIndex::Build(Entries, -1, NeverCancel, Index));
	ExpectBuildFailure(TEXT("hard cap"),
		FVoxelLandM5SpatialIndex::Build(Entries, FVoxelLandM5SpatialIndex::MaximumEntries + 1, NeverCancel, Index));
	ExpectBuildFailure(TEXT("cancelled build"), FVoxelLandM5SpatialIndex::Build(Entries, 2, AlwaysCancel, Index));
	Entries[1].PrimitiveId = 1;
	ExpectBuildFailure(TEXT("duplicate IDs"), FVoxelLandM5SpatialIndex::Build(Entries, 2, NeverCancel, Index));
	Entries[1].PrimitiveId = 2;
	Entries[1].Bounds.Min.X = std::numeric_limits<double>::quiet_NaN();
	ExpectBuildFailure(TEXT("nonfinite bound"), FVoxelLandM5SpatialIndex::Build(Entries, 2, NeverCancel, Index));
	Entries[1].Bounds.Min.X = 3;
	ExpectBuildFailure(TEXT("inverted bound"), FVoxelLandM5SpatialIndex::Build(Entries, 2, NeverCancel, Index));
	Entries[1].Bounds = FBox3d(ForceInit);
	ExpectBuildFailure(TEXT("uninitialized bound"), FVoxelLandM5SpatialIndex::Build(Entries, 2, NeverCancel, Index));

	TArray<uint64> Result = {77};
	const FBox3d Query(FVector3d(0, 0, 0), FVector3d(2, 2, 2));
	TestEqual(TEXT("cancel code"), int32(Index->Query(Query, AlwaysCancel, Result).Code),
		int32(EVoxelLandErrorCode::Cancelled));
	TestTrue(TEXT("cancelled query retains output"), Result == TArray<uint64>({77}));
	TestTrue(TEXT("invalid query rejected"), bool(Index->Query(FBox3d(ForceInit), NeverCancel, Result)));
	TestTrue(TEXT("invalid query retains output"), Result == TArray<uint64>({77}));

	int32 Calls = 0;
	const auto CancelAfterScan = [&Calls]() {
		return ++Calls == 2;
	};
	TestEqual(TEXT("late cancel code"), int32(Index->Query(Query, CancelAfterScan, Result).Code),
		int32(EVoxelLandErrorCode::Cancelled));
	TestTrue(TEXT("late cancellation retains output"), Result == TArray<uint64>({77}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5SpatialIndexBatchedCancellationTest, "VoxelLand.M5.Spatial.BatchedCancellation", M5SpatialTestFlags)
bool FVoxelLandM5SpatialIndexBatchedCancellationTest::RunTest(const FString& Parameters)
{
	TArray<FVoxelLandM5InfluenceBounds> Entries;
	for (int32 I = 0; I < 200; ++I)
	{
		Entries.Add(M5TestBounds(uint64(I), FVector3d(I, 0, 0), FVector3d(I + 1, 1, 1)));
	}
	const auto NeverCancel = []() {
		return false;
	};
	TSharedPtr<const FVoxelLandM5SpatialIndex, ESPMode::ThreadSafe> Index;
	const FVoxelLandError InitialError = FVoxelLandM5SpatialIndex::Build(Entries, Entries.Num(), NeverCancel, Index);
	if (!TestFalse(TEXT("initial index built"), bool(InitialError))
		|| !TestTrue(TEXT("index published"), Index.IsValid()))
	{
		return false;
	}
	const FVoxelLandM5SpatialIndex* Original = Index.Get();

	// Calls 1 and 2 enter the first two outer iterations; call 3 is the
	// first duplicate-comparison batch. Cancellation occurs at that boundary.
	int32 BuildCalls = 0;
	const auto CancelInDuplicateScan = [&BuildCalls]() {
		return ++BuildCalls == 3;
	};
	const FVoxelLandError BuildError =
		FVoxelLandM5SpatialIndex::Build(Entries, Entries.Num(), CancelInDuplicateScan, Index);
	TestEqual(TEXT("duplicate scan cancellation code"), int32(BuildError.Code), int32(EVoxelLandErrorCode::Cancelled));
	TestEqual(TEXT("cancelled during duplicate batch"), BuildCalls, 3);
	TestTrue(TEXT("cancelled duplicate scan retains original index"), Index.Get() == Original);

	// The query checks cancellation at indices 0, 64, and 128. The third
	// check occurs only after two batches have added matching IDs to scratch.
	int32 QueryCalls = 0;
	const auto CancelInThirdBatch = [&QueryCalls]() {
		return ++QueryCalls == 3;
	};
	TArray<uint64> Result = {9999};
	const FBox3d Query(FVector3d(-1, -1, -1), FVector3d(201, 2, 2));
	const FVoxelLandError QueryError = Index->Query(Query, CancelInThirdBatch, Result);
	TestEqual(TEXT("query batch cancellation code"), int32(QueryError.Code), int32(EVoxelLandErrorCode::Cancelled));
	TestEqual(TEXT("cancelled during third query batch"), QueryCalls, 3);
	TestTrue(TEXT("cancelled batched query preserves output"), Result == TArray<uint64>({9999}));
	return true;
}
#endif
