#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5RouteBuilder.h"
#include "Generation/VoxelLandM5ScratchChunks.h"
#include "Generation/VoxelLandM5ScratchMesh.h"
#include "Generation/VoxelLandM5SupportSelector.h"
#include "Misc/AutomationTest.h"

class FVoxelLandM5SupportSelectorFixture final
{
public:
	static TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate(
		const FVoxelLandM5CaveSettings& Settings, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget)
	{
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
		if (!Budget->TryReserveBytes(65536, Lease)) return nullptr;
		FVoxelLandM5CavePlan Plan;
		for (int32 I = 0; I < 2; ++I)
		{
			FVoxelLandM5Chamber Chamber;
			Chamber.bHasId = true; Chamber.Id = uint64(I + 1);
			Chamber.CenterCm.X = -150 + 300 * I; Chamber.CenterCm.Y = 1450; Chamber.CenterCm.Z = 942;
			Chamber.RadiusCm = 150; Plan.Chambers.Add(Chamber);
		}
		FVoxelLandM5Connection Connection;
		Connection.bHasId = true; Connection.Id = 7;
		Connection.Start.bValid = Connection.End.bValid = true;
		Connection.Start.Id = 1; Connection.End.Id = 2; Plan.Connections.Add(Connection);
		FVoxelLandM5Segment Segment;
		Segment.bHasId = Segment.bHasConnectionId = true; Segment.Id = 9; Segment.ConnectionId = 7;
		Segment.StartCm = Plan.Chambers[0].CenterCm; Segment.EndCm = Plan.Chambers[1].CenterCm;
		Segment.RadiusCm = 103; Plan.Segments.Add(Segment);
		const FVoxelLandM5RegionCoord Region;
		return MakeShareable(new FVoxelLandM5CandidateResult(Budget, Settings, 11, Region, MoveTemp(Plan), MoveTemp(Lease)));
	}
	static TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> EmptyRoute(
		const FVoxelLandM5CaveSettings& Settings, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Source,
		const FVoxelLandM5CandidateResult& Routed)
	{
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
		if (!Budget->TryReserveBytes(65536, Lease)) return nullptr;
		FVoxelLandM5CavePlan Plan = Routed.GetPlan();
		if (Plan.Routes.IsEmpty()) return nullptr;
		Plan.Routes[0].Legs.Reset();
		const FVoxelLandM5RegionCoord Region = Plan.Region;
		TSharedPtr<FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Result =
			MakeShareable(new FVoxelLandM5CandidateResult(Budget, Settings, 11, Region, MoveTemp(Plan), MoveTemp(Lease)));
		Result->DirectSource = Source;
		return Result;
	}
	static TSharedPtr<const FVoxelLandM5ScratchMesh, ESPMode::ThreadSafe> Analytic(
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TSharedRef<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Source)
	{
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
		if (!Budget->TryReserveBytes(65536, Lease)) return nullptr;
		TSharedPtr<FVoxelLandM5ScratchMesh, ESPMode::ThreadSafe> Result = MakeShareable(new FVoxelLandM5ScratchMesh(MoveTemp(Lease), Source));
		constexpr int64 Q = int64(1) << 18;
		FVoxelLandM5FixedTriangleQ18 Up;
		Up.A.X = -1000000 * Q; Up.A.Y = -1000000 * Q; Up.A.Z = 850 * Q;
		Up.B.X = 1000000 * Q - 1; Up.B.Y = -1000000 * Q; Up.B.Z = 850 * Q;
		Up.C.X = 0; Up.C.Y = 1000000 * Q - 1; Up.C.Z = 850 * Q;
		Up.SourceTriangle = 3;
		FVoxelLandM5FixedTriangleQ18 Down = Up;
		Swap(Down.B, Down.C); Down.SourceTriangle = 4;
		FVoxelLandM5FixedTriangleQ18 Wall = Up;
		Wall.A.X = 0; Wall.A.Y = 1400 * Q; Wall.B.X = 0; Wall.B.Y = 1500 * Q;
		Wall.C.X = 0; Wall.C.Y = 1400 * Q; Wall.C.Z += 100 * Q; Wall.SourceTriangle = 5;
		Result->Triangles = {Up, Down, Wall};
		for (int32 I = 3; I <= 5; ++I)
		{
			FVoxelLandM5ChunkFaceSelection Face; Face.ChunkCoordinate = FIntVector(0, 0, 0); Face.FaceIndex = I;
			Result->Faces.Add(Face);
		}
		return Result;
	}
	static TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Chunks(
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Source, bool bReverse)
	{
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
		if (!Budget->TryReserveBytes(1024 * 1024, Lease)) return nullptr;
		TSharedPtr<FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Result = MakeShareable(new FVoxelLandM5ScratchChunks(MoveTemp(Lease)));
		Result->SourceCandidate = Source; Result->MinInclusive = FIntVector(-1, 0, 0);
		Result->MaxExclusive = FIntVector(1, 1, 1); Result->VoxelSizeCm = 100;
		for (int32 XChunk = -1; XChunk <= 0; ++XChunk)
		{
			auto& Chunk = Result->Chunks.AddDefaulted_GetRef();
			Chunk.Key.Coordinate = FIntVector(XChunk, 0, 0);
			Chunk.Density.SetNumUninitialized(FVoxelLandChunkData::SampleCount);
			for (int32 Z = -1; Z <= 33; ++Z)
				for (int32 Y = -1; Y <= 33; ++Y)
					for (int32 X = -1; X <= 33; ++X)
						Chunk.Density[(X + 1) + 35 * ((Y + 1) + 35 * (Z + 1))] = Z <= 8 ? -100 : 100;
		}
		if (bReverse) Swap(Result->Chunks[0], Result->Chunks[1]);
		return Result;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5SupportSelectorTest, "VoxelLand.M5.SupportSelector.ActualSeam",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM5SupportSelectorTest::RunTest(const FString& Parameters)
{
	bool bSuccess = false, bReverse = false, bCap = false, bExactCap = false, bForeign = false;
	bool bCancelled = false, bScanCancelled = false, bUnrelated = false, bWide = false, bEmpty = false;
	FString Failure;
	Async(EAsyncExecution::ThreadPool, [&] {
		const auto Never = [] { return false; };
		FVoxelLandM5CaveSettings Settings;
		Settings.bEnabled = true; Settings.StageId = FGuid(1, 2, 3, 4);
		Settings.Topology.MinChambers = Settings.Topology.MaxChambers = 2;
		Settings.Topology.EntranceCount = 0; Settings.Topology.MinLoops = Settings.Topology.MaxLoops = 0;
		Settings.Topology.MinTunnelRadiusCm = Settings.Topology.MaxTunnelRadiusCm = 103;
		Settings.Topology.MinChamberRadiusCm = Settings.Topology.MaxChamberRadiusCm = 150;
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, Other;
		if (!FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 1000000000, Budget)
			|| !FVoxelLandM5Budget::TryCreate(256ll * 1024 * 1024, 1000000000, Other)) return;
		auto Source = FVoxelLandM5SupportSelectorFixture::Candidate(Settings, Budget.ToSharedRef());
		auto Equal = FVoxelLandM5SupportSelectorFixture::Candidate(Settings, Budget.ToSharedRef());
		if (!Source || !Equal) return;
		TArray<FVoxelLandM5RouteAnchorChoice> Anchors;
		for (int32 I = 0; I < 2; ++I)
		{
			FVoxelLandM5RouteAnchorChoice Anchor;
			Anchor.Endpoint.bValid = true; Anchor.Endpoint.Id = I + 1;
			Anchor.Endpoint.AnchorRole = EVoxelLandM5AnchorRole::ChamberFeet;
			Anchor.FeetCm = Source->GetPlan().Chambers[I].CenterCm; Anchor.FeetCm.Z = 850;
			Anchors.Add(Anchor);
		}
		FVoxelLandM5RouteConnectionChoice Choice;
		Choice.bHasConnectionId = true; Choice.ConnectionId = 7; Choice.Start = Anchors[0].Endpoint;
		Choice.End = Anchors[1].Endpoint; Choice.SegmentIds.Add(9);
		TArray<FVoxelLandM5RouteConnectionChoice> Choices; Choices.Add(Choice);
		TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Routed;
		auto Result = VoxelLand::M5::RouteBuilder::Build(Settings, 11, Source->GetPlan().Region,
			Source.ToSharedRef(), Anchors, Choices, Budget.ToSharedRef(), Never, Routed);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Routed) { Failure = Result.Message; return; }
		auto ChunkSource = FVoxelLandM5SupportSelectorFixture::Chunks(Budget.ToSharedRef(), Source.ToSharedRef(), false);
		auto ReverseSource = FVoxelLandM5SupportSelectorFixture::Chunks(Budget.ToSharedRef(), Source.ToSharedRef(), true);
		if (!ChunkSource || !ReverseSource) return;
		TSharedPtr<const FVoxelLandM5ScratchMesh, ESPMode::ThreadSafe> Mesh, ReverseMesh;
		Result = FVoxelLandM5ScratchMesh::Build(ChunkSource.ToSharedRef(), 2, 10000, Budget.ToSharedRef(), Never, Mesh);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Mesh) { Failure = Result.Message; return; }
		Result = FVoxelLandM5ScratchMesh::Build(ReverseSource.ToSharedRef(), 2, 10000, Budget.ToSharedRef(), Never, ReverseMesh);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !ReverseMesh) { Failure = Result.Message; return; }
		TSharedPtr<const FVoxelLandM5SupportSelector, ESPMode::ThreadSafe> Selected, Reversed;
		Result = FVoxelLandM5SupportSelector::Build(Settings, 11, Source.ToSharedRef(), Routed.ToSharedRef(), Mesh.ToSharedRef(), 0, 256, Budget.ToSharedRef(), Never, Selected);
		if (Result.Code != EVoxelLandM5ErrorCode::None || !Selected) { Failure = Result.Message; return; }
		bSuccess = Selected->MatchesSources(*Mesh, *Routed) && Selected->GetSelections().Num() == 1;
		if (!bSuccess) return;
		const auto& Faces = Selected->GetSelections()[0].SupportFaces;
		bool bLeft = false, bRight = false;
		for (const auto& Face : Faces) { bLeft |= Face.ChunkCoordinate.X == -1; bRight |= Face.ChunkCoordinate.X == 0; }
		bSuccess &= bLeft && bRight;
		// A flat z-edge at (x,y,8) emits two faces, ordered by y then x.
		// Its quad spans cell centers x +/- 50 and y +/- 50 cm.
		const int32 ExpectedLeft[] = {956, 957, 958, 959, 1020, 1021, 1022, 1023};
		const int32 ExpectedRight[] = {896, 897, 898, 899, 900, 901, 960, 961, 962, 963, 964, 965};
		bSuccess &= Faces.Num() == UE_ARRAY_COUNT(ExpectedLeft) + UE_ARRAY_COUNT(ExpectedRight);
		if (bSuccess)
		{
			for (int32 I = 0; I < UE_ARRAY_COUNT(ExpectedLeft); ++I)
				bSuccess &= Faces[I].ChunkCoordinate == FIntVector(-1, 0, 0) && Faces[I].FaceIndex == ExpectedLeft[I];
			for (int32 I = 0; I < UE_ARRAY_COUNT(ExpectedRight); ++I)
				bSuccess &= Faces[UE_ARRAY_COUNT(ExpectedLeft) + I].ChunkCoordinate == FIntVector(0, 0, 0)
					&& Faces[UE_ARRAY_COUNT(ExpectedLeft) + I].FaceIndex == ExpectedRight[I];
		}
		Result = FVoxelLandM5SupportSelector::Build(Settings, 11, Source.ToSharedRef(), Routed.ToSharedRef(), ReverseMesh.ToSharedRef(), 0, 256, Budget.ToSharedRef(), Never, Reversed);
		bReverse = Result.Code == EVoxelLandM5ErrorCode::None && Reversed && Reversed->GetSelections().Num() == 1
			&& Reversed->GetSelections()[0].SupportFaces.Num() == Faces.Num();
		if (bReverse) for (int32 I = 0; I < Faces.Num(); ++I)
			bReverse &= Faces[I].ChunkCoordinate == Reversed->GetSelections()[0].SupportFaces[I].ChunkCoordinate
				&& Faces[I].FaceIndex == Reversed->GetSelections()[0].SupportFaces[I].FaceIndex;
		auto AnalyticMesh = FVoxelLandM5SupportSelectorFixture::Analytic(Budget.ToSharedRef(), ChunkSource.ToSharedRef());
		TSharedPtr<const FVoxelLandM5SupportSelector, ESPMode::ThreadSafe> AnalyticSelection;
		if (AnalyticMesh)
		{
			Result = FVoxelLandM5SupportSelector::Build(Settings, 11, Source.ToSharedRef(), Routed.ToSharedRef(), AnalyticMesh.ToSharedRef(), 0, 256, Budget.ToSharedRef(), Never, AnalyticSelection);
			bWide = Result.Code == EVoxelLandM5ErrorCode::None && AnalyticSelection && AnalyticSelection->GetSelections().Num() == 1
				&& AnalyticSelection->GetSelections()[0].SupportFaces.Num() == 1
				&& AnalyticSelection->GetSelections()[0].SupportFaces[0].FaceIndex == 3;
		}
		const auto Previous = Selected;
		TSharedPtr<const FVoxelLandM5SupportSelector, ESPMode::ThreadSafe> Exact;
		Result = FVoxelLandM5SupportSelector::Build(Settings, 11, Source.ToSharedRef(), Routed.ToSharedRef(), Mesh.ToSharedRef(), 0, Faces.Num(), Budget.ToSharedRef(), Never, Exact);
		bExactCap = Result.Code == EVoxelLandM5ErrorCode::None && Exact && Exact->GetSelections()[0].SupportFaces.Num() == Faces.Num();
		Result = FVoxelLandM5SupportSelector::Build(Settings, 11, Source.ToSharedRef(), Routed.ToSharedRef(), Mesh.ToSharedRef(), 0, Faces.Num() - 1, Budget.ToSharedRef(), Never, Selected);
		bCap = Result.Code == EVoxelLandM5ErrorCode::BudgetExceeded && Selected == Previous;
		Result = FVoxelLandM5SupportSelector::Build(Settings, 11, Source.ToSharedRef(), Routed.ToSharedRef(), Mesh.ToSharedRef(), 0, 256, Other.ToSharedRef(), Never, Selected);
		bForeign = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Selected == Previous;
		Result = FVoxelLandM5SupportSelector::Build(Settings, 11, Equal.ToSharedRef(), Routed.ToSharedRef(), Mesh.ToSharedRef(), 0, 256, Budget.ToSharedRef(), Never, Selected);
		bUnrelated = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Selected == Previous;
		auto Empty = FVoxelLandM5SupportSelectorFixture::EmptyRoute(Settings, Budget.ToSharedRef(), Source.ToSharedRef(), *Routed);
		if (Empty)
		{
			Result = FVoxelLandM5SupportSelector::Build(Settings, 11, Source.ToSharedRef(), Empty.ToSharedRef(), Mesh.ToSharedRef(), 0, 256, Budget.ToSharedRef(), Never, Selected);
			bEmpty = Result.Code == EVoxelLandM5ErrorCode::InvalidInput && Selected == Previous;
		}
		Result = FVoxelLandM5SupportSelector::Build(Settings, 11, Source.ToSharedRef(), Routed.ToSharedRef(), Mesh.ToSharedRef(), 0, 256, Budget.ToSharedRef(), [] { return true; }, Selected);
		bCancelled = Result.Code == EVoxelLandM5ErrorCode::Cancelled && Selected == Previous;
		int32 Polls = 0;
		Result = FVoxelLandM5SupportSelector::Build(Settings, 11, Source.ToSharedRef(), Routed.ToSharedRef(), Mesh.ToSharedRef(), 0, 256, Budget.ToSharedRef(), [&Polls] { return ++Polls >= 8; }, Selected);
		bScanCancelled = Result.Code == EVoxelLandM5ErrorCode::Cancelled && Selected == Previous && Polls == 8;

	}).Wait();
	TestTrue(FString::Printf(TEXT("actual seam selection: %s"), *Failure), bSuccess);
	TestTrue(TEXT("reversed storage yields same faces"), bReverse);
	TestTrue(TEXT("wide positive winding selected, downward and vertical excluded"), bWide);
	TestTrue(TEXT("face cap at demand succeeds"), bExactCap);
	TestTrue(TEXT("face cap below demand preserves output"), bCap);
	TestTrue(TEXT("foreign budget preserves output"), bForeign);
	TestTrue(TEXT("unrelated candidate preserves output"), bUnrelated);
	TestTrue(TEXT("empty route preserves output"), bEmpty);
	TestTrue(TEXT("cancellation preserves output"), bCancelled);
	TestTrue(TEXT("scan cancellation preserves output"), bScanCancelled);
	return bSuccess && bReverse && bWide && bCap && bExactCap && bForeign && bUnrelated && bEmpty && bCancelled && bScanCancelled;
}
#endif
