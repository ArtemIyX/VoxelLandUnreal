#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/Caves/Planning/VoxelLandCaveSpatialIndex.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Planning/VoxelLandCaveSpatialQueryResult.h"
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

FVoxelLandError M5TestQuery(const FVoxelLandM5SpatialIndex& Index, const FBox3d& Bounds,
	TFunctionRef<bool()> IsCancelled, TArray<uint64>& OutIds)
{
	TSharedPtr<const FVoxelLandM5SpatialQueryResult, ESPMode::ThreadSafe> Result;
	FVoxelLandError Error = Index.Query(Bounds, IsCancelled, Result);
	if (!Error && Result.IsValid())
	{
		const TConstArrayView<uint64> Ids = Result->GetIds();
		OutIds.Reset();
		if (Ids.Num() > 0)
		{
			OutIds.Append(Ids.GetData(), Ids.Num());
		}
	}
	return Error;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5SpatialIndexReferenceTest, "VoxelLand.M5.Spatial.Reference", M5SpatialTestFlags)
bool FVoxelLandM5SpatialIndexReferenceTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"), FVoxelLandM5Budget::TryCreate(1024 * 1024, 1000000, Budget)))
	{
		return false;
	}
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
	TestFalse(TEXT("build succeeds"),
		bool(FVoxelLandM5SpatialIndex::Build(Entries, 5, Budget.ToSharedRef(), NeverCancel, Index)));
	if (!TestTrue(TEXT("index was published"), Index.IsValid()))
	{
		return false;
	}
	TArray<uint64> PointResult;
	const FBox3d Origin(FVector3d(0, 0, 0), FVector3d(0, 0, 0));
	TestFalse(TEXT("point query succeeds"), bool(M5TestQuery(*Index, Origin, NeverCancel, PointResult)));
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
			const FVoxelLandError Error = M5TestQuery(*Index, Query, NeverCancel, Actual);
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
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"), FVoxelLandM5Budget::TryCreate(1024 * 1024, 1000000, Budget)))
	{
		return false;
	}
	const auto NeverCancel = []() {
		return false;
	};
	const auto AlwaysCancel = []() {
		return true;
	};
	TArray<FVoxelLandM5InfluenceBounds> Entries = {M5TestBounds(1, FVector3d(0, 0, 0), FVector3d(1, 1, 1)),
		M5TestBounds(2, FVector3d(1, 1, 1), FVector3d(2, 2, 2))};
	TSharedPtr<const FVoxelLandM5SpatialIndex, ESPMode::ThreadSafe> Index;
	TestFalse(TEXT("initial build"),
		bool(FVoxelLandM5SpatialIndex::Build(Entries, 2, Budget.ToSharedRef(), NeverCancel, Index)));
	if (!TestTrue(TEXT("initial index valid"), Index.IsValid()))
	{
		return false;
	}
	const FVoxelLandM5SpatialIndex* Original = Index.Get();
	const auto ExpectBuildFailure = [this, &Index, Original](const TCHAR* Label, const FVoxelLandError& Error) {
		TestTrue(Label, bool(Error));
		TestTrue(TEXT("failed build retains existing index"), Index.Get() == Original);
	};
	ExpectBuildFailure(
		TEXT("entry budget"), FVoxelLandM5SpatialIndex::Build(Entries, 1, Budget.ToSharedRef(), NeverCancel, Index));
	ExpectBuildFailure(TEXT("negative budget"),
		FVoxelLandM5SpatialIndex::Build(Entries, -1, Budget.ToSharedRef(), NeverCancel, Index));
	ExpectBuildFailure(
		TEXT("hard cap"), FVoxelLandM5SpatialIndex::Build(Entries, FVoxelLandM5SpatialIndex::MaximumEntries + 1,
							  Budget.ToSharedRef(), NeverCancel, Index));
	ExpectBuildFailure(TEXT("cancelled build"),
		FVoxelLandM5SpatialIndex::Build(Entries, 2, Budget.ToSharedRef(), AlwaysCancel, Index));
	Entries[1].PrimitiveId = 1;
	ExpectBuildFailure(
		TEXT("duplicate IDs"), FVoxelLandM5SpatialIndex::Build(Entries, 2, Budget.ToSharedRef(), NeverCancel, Index));
	Entries[1].PrimitiveId = 2;
	Entries[1].Bounds.Min.X = std::numeric_limits<double>::quiet_NaN();
	ExpectBuildFailure(
		TEXT("nonfinite bound"), FVoxelLandM5SpatialIndex::Build(Entries, 2, Budget.ToSharedRef(), NeverCancel, Index));
	Entries[1].Bounds.Min.X = 3;
	ExpectBuildFailure(
		TEXT("inverted bound"), FVoxelLandM5SpatialIndex::Build(Entries, 2, Budget.ToSharedRef(), NeverCancel, Index));
	Entries[1].Bounds = FBox3d(ForceInit);
	ExpectBuildFailure(TEXT("uninitialized bound"),
		FVoxelLandM5SpatialIndex::Build(Entries, 2, Budget.ToSharedRef(), NeverCancel, Index));

	TArray<uint64> Result = {77};
	const FBox3d Query(FVector3d(0, 0, 0), FVector3d(2, 2, 2));
	TestEqual(TEXT("cancel code"), int32(M5TestQuery(*Index, Query, AlwaysCancel, Result).Code),
		int32(EVoxelLandErrorCode::Cancelled));
	TestTrue(TEXT("cancelled query retains output"), Result == TArray<uint64>({77}));
	TestTrue(TEXT("invalid query rejected"), bool(M5TestQuery(*Index, FBox3d(ForceInit), NeverCancel, Result)));
	TestTrue(TEXT("invalid query retains output"), Result == TArray<uint64>({77}));

	int32 Calls = 0;
	const auto CancelAfterScan = [&Calls]() {
		return ++Calls == 3;
	};
	TestEqual(TEXT("late cancel code"), int32(M5TestQuery(*Index, Query, CancelAfterScan, Result).Code),
		int32(EVoxelLandErrorCode::Cancelled));
	TestTrue(TEXT("late cancellation retains output"), Result == TArray<uint64>({77}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5SpatialIndexBatchedCancellationTest, "VoxelLand.M5.Spatial.BatchedCancellation", M5SpatialTestFlags)
bool FVoxelLandM5SpatialIndexBatchedCancellationTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"), FVoxelLandM5Budget::TryCreate(1024 * 1024, 1000000, Budget)))
	{
		return false;
	}
	TArray<FVoxelLandM5InfluenceBounds> Entries;
	for (int32 I = 0; I < 200; ++I)
	{
		Entries.Add(M5TestBounds(uint64(I), FVector3d(I, 0, 0), FVector3d(I + 1, 1, 1)));
	}
	const auto NeverCancel = []() {
		return false;
	};
	TSharedPtr<const FVoxelLandM5SpatialIndex, ESPMode::ThreadSafe> Index;
	const FVoxelLandError InitialError =
		FVoxelLandM5SpatialIndex::Build(Entries, Entries.Num(), Budget.ToSharedRef(), NeverCancel, Index);
	if (!TestFalse(TEXT("initial index built"), bool(InitialError))
		|| !TestTrue(TEXT("index published"), Index.IsValid()))
	{
		return false;
	}
	const FVoxelLandM5SpatialIndex* Original = Index.Get();

	// Call 1 is pre-admission; calls 2 and 3 enter the first outer iterations; call 4 is the
	// first duplicate-comparison batch. Cancellation occurs at that boundary.
	int32 BuildCalls = 0;
	const auto CancelInDuplicateScan = [&BuildCalls]() {
		return ++BuildCalls == 4;
	};
	const FVoxelLandError BuildError =
		FVoxelLandM5SpatialIndex::Build(Entries, Entries.Num(), Budget.ToSharedRef(), CancelInDuplicateScan, Index);
	TestEqual(TEXT("duplicate scan cancellation code"), int32(BuildError.Code), int32(EVoxelLandErrorCode::Cancelled));
	TestEqual(TEXT("cancelled during duplicate batch"), BuildCalls, 4);
	TestTrue(TEXT("cancelled duplicate scan retains original index"), Index.Get() == Original);

	// After the initial check the query checks indices 0, 64, and 128. The final
	// check occurs only after two batches have added matching IDs to scratch.
	int32 QueryCalls = 0;
	const auto CancelInThirdBatch = [&QueryCalls]() {
		return ++QueryCalls == 4;
	};
	TArray<uint64> Result = {9999};
	const FBox3d Query(FVector3d(-1, -1, -1), FVector3d(201, 2, 2));
	const FVoxelLandError QueryError = M5TestQuery(*Index, Query, CancelInThirdBatch, Result);
	TestEqual(TEXT("query batch cancellation code"), int32(QueryError.Code), int32(EVoxelLandErrorCode::Cancelled));
	TestEqual(TEXT("cancelled during third query batch"), QueryCalls, 4);
	TestTrue(TEXT("cancelled batched query preserves output"), Result == TArray<uint64>({9999}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5SpatialIndexBudgetTest, "VoxelLand.M5.Spatial.BudgetLeases", M5SpatialTestFlags)
bool FVoxelLandM5SpatialIndexBudgetTest::RunTest(const FString& Parameters)
{
	const auto NeverCancel = []() {
		return false;
	};
	const TArray<FVoxelLandM5InfluenceBounds> Entries = {M5TestBounds(1, FVector3d(0, 0, 0), FVector3d(1, 1, 1)),
		M5TestBounds(2, FVector3d(1, 1, 1), FVector3d(2, 2, 2))};
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"), FVoxelLandM5Budget::TryCreate(1000000, 1000000, Budget)))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5SpatialIndex, ESPMode::ThreadSafe> Index;
	if (!TestFalse(TEXT("index built"),
			bool(FVoxelLandM5SpatialIndex::Build(Entries, 2, Budget.ToSharedRef(), NeverCancel, Index))))
	{
		return false;
	}
	int64 Base = 0, Peak = 0, Work = 0;
	Budget->GetUsage(Base, Peak, Work);
	TestTrue(TEXT("index lease remains charged"), Base > 0);
	const FBox3d Query(FVector3d(0, 0, 0), FVector3d(2, 2, 2));
	TSharedPtr<const FVoxelLandM5SpatialQueryResult, ESPMode::ThreadSafe> First, Second;
	TestFalse(TEXT("first query"), bool(Index->Query(Query, NeverCancel, First)));
	TestFalse(TEXT("second query"), bool(Index->Query(Query, NeverCancel, Second)));
	int64 Both = 0;
	Budget->GetUsage(Both, Peak, Work);
	TestTrue(TEXT("two live query outputs hold separate reservations"), Both > Base);
	First.Reset();
	int64 One = 0;
	Budget->GetUsage(One, Peak, Work);
	TestTrue(TEXT("releasing one output releases only its lease"), Base < One && One < Both);
	Index.Reset();
	int64 OutputOnly = 0;
	Budget->GetUsage(OutputOnly, Peak, Work);
	TestTrue(TEXT("result keeps its lease after index destruction"), OutputOnly > 0 && OutputOnly < One);
	Second.Reset();
	Budget->GetUsage(OutputOnly, Peak, Work);
	TestEqual(TEXT("all leases returned"), OutputOnly, int64(0));

	// Determine a budget that admits the index but is one byte short of its
	// two-entry query reservation. Failure cannot replace the previous result.
	const int64 QueryBytes = 4096 + 4 * 2 * int64(sizeof(uint64));
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Tight;
	TestTrue(TEXT("tight budget created"), FVoxelLandM5Budget::TryCreate(Base + QueryBytes - 1, 1000000, Tight));
	TestFalse(TEXT("tight index build"),
		bool(FVoxelLandM5SpatialIndex::Build(Entries, 2, Tight.ToSharedRef(), NeverCancel, Index)));
	if (!Index.IsValid())
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5SpatialQueryResult, ESPMode::ThreadSafe> FailedResult;
	TestTrue(TEXT("query memory exhaustion"), bool(Index->Query(Query, NeverCancel, FailedResult)));
	TestFalse(TEXT("no partial query result"), FailedResult.IsValid());
	int64 After = 0;
	Tight->GetUsage(After, Peak, Work);
	TestEqual(TEXT("failed admission does not leak bytes"), After, Base);

	const FVoxelLandM5SpatialIndex* Original = Index.Get();
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> NoWork;
	TestTrue(TEXT("zero-work budget created"), FVoxelLandM5Budget::TryCreate(1000000, 0, NoWork));
	TestTrue(TEXT("work budget failure"),
		bool(FVoxelLandM5SpatialIndex::Build(Entries, 2, NoWork.ToSharedRef(), NeverCancel, Index)));
	TestTrue(TEXT("work failure preserves index"), Index.Get() == Original);
	NoWork->GetUsage(After, Peak, Work);
	TestEqual(TEXT("work failure releases reserved bytes"), After, int64(0));
	TestTrue(TEXT("overflow-sized limit rejected"),
		bool(FVoxelLandM5SpatialIndex::Build(Entries, MAX_int32, Tight.ToSharedRef(), NeverCancel, Index)));
	TestTrue(TEXT("oversized limit preserves index"), Index.Get() == Original);

	// Keep a successful output across failed queries, including failures using a
	// different index/budget. The old result owns its independent original lease.
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> ExactWork;
	TestTrue(TEXT("exact work budget created"), FVoxelLandM5Budget::TryCreate(1000000, 64, ExactWork));
	TSharedPtr<const FVoxelLandM5SpatialIndex, ESPMode::ThreadSafe> WorkIndex;
	TestFalse(TEXT("work-budget index built"),
		bool(FVoxelLandM5SpatialIndex::Build(Entries, 2, ExactWork.ToSharedRef(), NeverCancel, WorkIndex)));
	if (!WorkIndex.IsValid())
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5SpatialQueryResult, ESPMode::ThreadSafe> Previous;
	TestFalse(TEXT("first query fits work budget"), bool(WorkIndex->Query(Query, NeverCancel, Previous)));
	if (!Previous.IsValid())
	{
		return false;
	}
	const FVoxelLandM5SpatialQueryResult* PreviousPointer = Previous.Get();
	const auto ExpectPrevious = [this, &Previous, PreviousPointer]() {
		TestTrue(TEXT("failed query retains previous object"), Previous.Get() == PreviousPointer);
		if (Previous.IsValid())
		{
			const TConstArrayView<uint64> Ids = Previous->GetIds();
			TestTrue(TEXT("failed query preserves previous IDs"), Ids.Num() == 2 && Ids[0] == 1 && Ids[1] == 2);
		}
	};
	TestTrue(TEXT("subsequent work exhaustion"), bool(WorkIndex->Query(Query, NeverCancel, Previous)));
	ExpectPrevious();
	TestTrue(TEXT("subsequent memory exhaustion"), bool(Index->Query(Query, NeverCancel, Previous)));
	ExpectPrevious();
	const auto AlwaysCancel = []() {
		return true;
	};
	TestEqual(TEXT("subsequent cancellation"), int32(Index->Query(Query, AlwaysCancel, Previous).Code),
		int32(EVoxelLandErrorCode::Cancelled));
	ExpectPrevious();
	return true;
}
#endif
