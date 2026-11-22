#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5CandidatePlanner.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5RegionClosure.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5ClosureFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

bool M5ClosureSame(const FVoxelLandM5RegionCoord& A, const FVoxelLandM5RegionCoord& B)
{
	return A.X == B.X && A.Y == B.Y;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5RegionClosureTest, "VoxelLand.M5.Regional.RegionClosure", M5ClosureFlags)
bool FVoxelLandM5RegionClosureTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5CaveSettings S;
	S.bEnabled = true;
	S.StageId = FGuid(1, 2, 3, 4);
	S.PlanningMode = EVoxelLandM5PlanningMode::Regional;
	S.Bounds.MinInclusive.X = S.Bounds.MinInclusive.Y = -200;
	S.Bounds.MaxExclusive.X = S.Bounds.MaxExclusive.Y = 200;
	S.Bounds.MinInclusive.Z = -100;
	S.Bounds.MaxExclusive.Z = 100;
	S.RegionSizeCm = 200;
	S.Topology.MinChambers = S.Topology.MaxChambers = 1;
	// These are raw candidates, before the separate entrance-planning stage.
	S.Topology.EntranceCount = 0;
	S.Topology.MinLoops = S.Topology.MaxLoops = 0;
	S.Topology.MinTunnelRadiusCm = 1;
	S.Topology.MaxTunnelRadiusCm = 2;
	S.Topology.MinChamberRadiusCm = S.Topology.MaxChamberRadiusCm = 2;
	S.Limits.MaxCandidates = 1;
	S.Limits.MaxEdges = S.Limits.MaxSegments = 32;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("create budget"),
			FVoxelLandM5Budget::TryCreate(S.Limits.MaxBytes, S.Limits.MaxValidationWork, Budget)))
	{
		return false;
	}
	const auto NeverCancel = [] {
		return false;
	};
	const TArray<uint8> Descriptor = {1, 2, 0, 4};
	TArray<FVoxelLandM5RegionCoord> Required;
	TArray<FVoxelLandM5RegionInput> Regions;
	Required.Reserve(4);
	Regions.Reserve(4);
	for (int64 X = -1; X <= 0; ++X)
	{
		for (int64 Y = -1; Y <= 0; ++Y)
		{
			FVoxelLandM5RegionInput Input;
			Input.Region.X = X;
			Input.Region.Y = Y;
			Input.Descriptor = Descriptor;
			const FVoxelLandM5Diagnostic D = VoxelLand::M5::CandidatePlanner::Build(
				S, 42, Input.Region, Budget.ToSharedRef(), NeverCancel, Input.Candidate);
			if (!TestTrue(TEXT("regional scratch candidate"),
					D.Code == EVoxelLandM5ErrorCode::None && Input.Candidate.IsValid()))
			{
				return false;
			}
			Required.Add(Input.Region);
			Regions.Add(MoveTemp(Input));
		}
	}
	TArray<int16> SharedSamples;
	SharedSamples.Init(-20, 343);
	TArray<FVoxelLandM5RegionEdgeInput> Edges;
	Edges.Reserve(4);
	for (int32 I = 0; I < Regions.Num(); ++I)
	{
		for (int32 J = I + 1; J < Regions.Num(); ++J)
		{
			const FVoxelLandM5RegionCoord A = Regions[I].Region, B = Regions[J].Region;
			if (!((A.X == B.X && FMath::Abs(A.Y - B.Y) == 1) || (A.Y == B.Y && FMath::Abs(A.X - B.X) == 1)))
			{
				continue;
			}
			const FVoxelLandM5Portal* Portal = nullptr;
			for (const FVoxelLandM5Portal& P : Regions[I].Candidate->GetPlan().Portals)
			{
				if (M5ClosureSame(P.Key.RegionA, B) || M5ClosureSame(P.Key.RegionB, B))
				{
					Portal = &P;
				}
			}
			if (!TestNotNull(TEXT("independent expected neighbor portal"), Portal))
			{
				return false;
			}
			FVoxelLandM5RegionEdgeInput Edge;
			Edge.A = A;
			Edge.B = B;
			Edge.SamplesA.OriginCm.X = Portal->CenterCm.X - 3;
			Edge.SamplesA.OriginCm.Y = Portal->CenterCm.Y - 3;
			Edge.SamplesA.OriginCm.Z = Portal->CenterCm.Z - 3;
			Edge.SamplesA.Counts = FIntVector(7, 7, 7);
			Edge.SamplesA.SpacingCm = 1;
			Edge.SamplesA.Samples = SharedSamples;
			Edge.SamplesB = Edge.SamplesA;
			Edges.Add(Edge);
		}
	}
	if (!TestEqual(TEXT("2x2 has four interior faces"), Edges.Num(), 4))
	{
		return false;
	}
	int32 Verified = 0;
	const auto Verify = [&Verified](TConstArrayView<uint8> Bytes, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>) {
		++Verified;
		FVoxelLandM5Diagnostic D;
		if (Bytes.Num() != 4)
		{
			D.Code = EVoxelLandM5ErrorCode::InvalidInput;
		}
		return D;
	};
	TSharedPtr<const FVoxelLandM5RegionClosure, ESPMode::ThreadSafe> Result;
	const auto Build = [&](TConstArrayView<FVoxelLandM5RegionCoord> Need,
						   TConstArrayView<FVoxelLandM5RegionInput> Inputs,
						   TConstArrayView<FVoxelLandM5RegionEdgeInput> Links, TFunctionRef<bool()> Cancel) {
		return VoxelLand::M5::RegionClosure::Build(
			S, 42, Need, Inputs, Links, Descriptor, 1, 1, Budget.ToSharedRef(), Verify, Cancel, Result);
	};
	if (!TestTrue(TEXT("valid negative 2x2 closure"),
			Build(Required, Regions, Edges, NeverCancel).Code == EVoxelLandM5ErrorCode::None && Result.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("descriptor checked twice"), Verified, 2);
	TestEqual(TEXT("all regions retained"), Result->GetRegions().Num(), 4);
	const auto Original = Result;
	TArray<FVoxelLandM5RegionCoord> Reversed = Required;
	Swap(Reversed[0], Reversed[3]);
	Swap(Reversed[1], Reversed[2]);
	TestTrue(TEXT("request order independent"),
		Build(Reversed, Regions, Edges, NeverCancel).Code == EVoxelLandM5ErrorCode::None);
	for (int32 I = 0; I < 4 && Result.IsValid(); ++I)
	{
		TestTrue(TEXT("canonical output ordering"), M5ClosureSame(Result->GetRegions()[I], Original->GetRegions()[I]));
	}
	const auto Retained = Result;
	TArray<FVoxelLandM5RegionInput> Missing = Regions;
	Missing.RemoveAt(2);
	TestTrue(TEXT("missing required region rejected"),
		Build(Required, Missing, Edges, NeverCancel).Code != EVoxelLandM5ErrorCode::None);
	TestTrue(TEXT("prior result preserved"), Result == Retained);
	TArray<FVoxelLandM5RegionEdgeInput> Hole = Edges;
	Hole.RemoveAt(0);
	TestTrue(TEXT("missing interior face rejected"),
		Build(Required, Regions, Hole, NeverCancel).Code == EVoxelLandM5ErrorCode::ProviderUnavailable);
	TestTrue(TEXT("failure preserves prior result"), Result == Retained);
	TArray<int16> Different = SharedSamples;
	Different[171] = 1;
	TArray<FVoxelLandM5RegionEdgeInput> Mismatch = Edges;
	Mismatch[0].SamplesB.Samples = Different;
	TestTrue(TEXT("mismatched portal overlap rejected"),
		Build(Required, Regions, Mismatch, NeverCancel).Code == EVoxelLandM5ErrorCode::InconsistentTopology);
	TestTrue(TEXT("cancelled before work"),
		Build(Required, Regions, Edges, [] { return true; }).Code == EVoxelLandM5ErrorCode::Cancelled);
	TestTrue(TEXT("cancel preserves prior result"), Result == Retained);
	TArray<FVoxelLandM5RegionCoord> Duplicate = Required;
	Duplicate[1] = Duplicate[0];
	TestTrue(TEXT("duplicate required coordinate rejected"),
		Build(Duplicate, Regions, Edges, NeverCancel).Code == EVoxelLandM5ErrorCode::InvalidInput);
	TArray<FVoxelLandM5RegionEdgeInput> DuplicateEdge = Edges;
	DuplicateEdge.Add(Edges[0]);
	TestTrue(TEXT("duplicate overlap rejected"),
		Build(Required, Regions, DuplicateEdge, NeverCancel).Code == EVoxelLandM5ErrorCode::InvalidInput);
	TArray<FVoxelLandM5RegionEdgeInput> UnexpectedEdge = Edges;
	FVoxelLandM5RegionEdgeInput SelfEdge;
	SelfEdge.A = Required[0];
	SelfEdge.B = Required[0];
	UnexpectedEdge.Add(SelfEdge);
	TestTrue(TEXT("unexpected non-neighbor overlap rejected"),
		Build(Required, Regions, UnexpectedEdge, NeverCancel).Code == EVoxelLandM5ErrorCode::InvalidInput);
	TArray<FVoxelLandM5RegionCoord> Disconnected = {Required[0], Required[3]};
	TArray<FVoxelLandM5RegionInput> DisconnectedInputs = {Regions[0], Regions[3]};
	TArray<FVoxelLandM5RegionEdgeInput> NoEdges;
	TestTrue(TEXT("complete but disconnected required set rejected"),
		Build(Disconnected, DisconnectedInputs, NoEdges, NeverCancel).Code == EVoxelLandM5ErrorCode::Blocked);
	TArray<FVoxelLandM5RegionCoord> Singleton = {Required[0]};
	TArray<FVoxelLandM5RegionInput> SingletonInputs = {Regions[0]};
	TSharedPtr<const FVoxelLandM5RegionClosure, ESPMode::ThreadSafe> SingleResult;
	TestTrue(TEXT("single region validates topology"),
		VoxelLand::M5::RegionClosure::Build(S, 42, Singleton, SingletonInputs, NoEdges, Descriptor, 1, 1,
			Budget.ToSharedRef(), Verify, NeverCancel, SingleResult)
					.Code
				== EVoxelLandM5ErrorCode::None
			&& SingleResult.IsValid() && SingleResult->GetRegions().Num() == 1);
	SingleResult.Reset();
	int64 LiveBeforePartial = 0, PeakBeforePartial = 0, WorkBeforePartial = 0;
	Budget->GetUsage(LiveBeforePartial, PeakBeforePartial, WorkBeforePartial);
	// The closure precharge is 4,160. Each of the 2-portal/3-node plans costs
	// 6,720 for topology, and one 7^3 handshake costs 8,558. Thus at 26,158
	// units beyond the starting count the first face has completed; the next
	// face has not yet completed when its precharge crosses this threshold.
	int64 WorkAtCancellation = 0;
	const auto CancelAfterFirstFace = [&Budget, WorkBeforePartial, &WorkAtCancellation] {
		int64 LiveNow = 0, PeakNow = 0, WorkNow = 0;
		Budget->GetUsage(LiveNow, PeakNow, WorkNow);
		if (WorkNow > WorkBeforePartial + 26158)
		{
			WorkAtCancellation = WorkNow;
			return true;
		}
		return false;
	};
	TestTrue(TEXT("cancellation after one completed face"),
		Build(Required, Regions, Edges, CancelAfterFirstFace).Code == EVoxelLandM5ErrorCode::Cancelled);
	TestTrue(TEXT("later face actually began"), WorkAtCancellation > WorkBeforePartial + 26158);
	TestTrue(TEXT("partial handshake cancellation preserves result"), Result == Retained);
	int32 DescriptorChecks = 0;
	const auto Stale = [&DescriptorChecks](
						   TConstArrayView<uint8>, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>) {
		FVoxelLandM5Diagnostic D;
		if (++DescriptorChecks == 2)
		{
			D.Code = EVoxelLandM5ErrorCode::InvalidInput;
		}
		return D;
	};
	TestTrue(TEXT("stale descriptor rejected at final check"),
		VoxelLand::M5::RegionClosure::Build(
			S, 42, Required, Regions, Edges, Descriptor, 1, 1, Budget.ToSharedRef(), Stale, NeverCancel, Result)
				.Code
			== EVoxelLandM5ErrorCode::InvalidInput);
	TestTrue(TEXT("stale result not published"), Result == Retained);
	int64 Live = 0, Peak = 0, Used = 0;
	Budget->GetUsage(Live, Peak, Used);
	int64 MaxBytes = 0, MaxWork = 0;
	Budget->GetLimits(MaxBytes, MaxWork);
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Occupied;
	if (!TestTrue(TEXT("reserve remaining memory"), Budget->TryReserveBytes(MaxBytes - Live, Occupied)))
	{
		return false;
	}
	TestTrue(TEXT("memory exhaustion fails closed"),
		Build(Required, Regions, Edges, NeverCancel).Code == EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("memory failure preserves result"), Result == Retained);
	Occupied.Reset();
	Budget->GetUsage(Live, Peak, Used);
	if (!TestTrue(TEXT("exhaust work budget"), Budget->TryChargeWork(MaxWork - Used)))
	{
		return false;
	}
	TestTrue(TEXT("budget exhaustion fails closed"),
		Build(Required, Regions, Edges, NeverCancel).Code == EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("budget failure preserves result"), Result == Retained);
	return true;
}
#endif
