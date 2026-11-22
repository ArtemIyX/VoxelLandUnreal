#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Structs/VoxelLandChunkData.h"
#include "Data/Structs/VoxelLandMeshData.h"
#include "Data/Structs/VoxelLandMeshingScratch.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5IdealRouteValidation.h"
#include "Generation/VoxelLandM5MeshAdapter.h"
#include "Generation/VoxelLandM5MultiChunkRoute.h"
#include "Generation/VoxelLandM5MultiRouteChecks.h"
#include "Generation/VoxelLandM5RouteBuilder.h"
#include "Generation/VoxelLandMeshing.h"
#include "Misc/AutomationTest.h"

class FVoxelLandM5MultiRoutesFixtureFactory
{
public:
	static TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Make(
		const FVoxelLandM5CaveSettings& Settings, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		FVoxelLandM5CavePlan Plan)
	{
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
		if (!Budget->TryReserveBytes(65536, Lease))
		{
			return nullptr;
		}
		const FVoxelLandM5RegionCoord Region = Plan.Region;
		return MakeShareable(
			new FVoxelLandM5CandidateResult(Budget, Settings, 11, Region, MoveTemp(Plan), MoveTemp(Lease)));
	}
};

namespace
{
FVoxelLandChunkData M5AllMultiRoutesChunk(int32 ChunkX, bool bWall)
{
	FVoxelLandChunkData Chunk;
	Chunk.Key.Coordinate = FIntVector(ChunkX, 0, 0);
	Chunk.Density.SetNumUninitialized(FVoxelLandChunkData::SampleCount);
	for (int32 Z = -1; Z <= 33; ++Z)
	{
		for (int32 Y = -1; Y <= 33; ++Y)
		{
			for (int32 X = -1; X <= 33; ++X)
			{
				const int32 GlobalX = ChunkX * 32 + X;
				const bool bSolid = Z <= 8 || (bWall && GlobalX == 2 && Z >= 10 && Z <= 11);
				Chunk.Density[(X + 1) + 35 * ((Y + 1) + 35 * (Z + 1))] = bSolid ? -100 : 100;
			}
		}
	}
	return Chunk;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5AllMultiRoutesTest, "VoxelLand.M5.MultiRouteChecks.AllIntervals",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM5AllMultiRoutesTest::RunTest(const FString& Parameters)
{
	const auto Never = [] {
		return false;
	};
	FVoxelLandM5CaveSettings Settings;
	Settings.StageId = FGuid(1, 2, 3, 4);
	Settings.Topology.MinChambers = 3;
	Settings.Topology.MaxChambers = 3;
	Settings.Topology.EntranceCount = 0;
	Settings.Topology.MinLoops = 0;
	Settings.Topology.MaxLoops = 0;
	Settings.Topology.MinTunnelRadiusCm = 103;
	Settings.Topology.MaxTunnelRadiusCm = 103;
	Settings.Topology.MinChamberRadiusCm = 150;
	Settings.Topology.MaxChamberRadiusCm = 150;
	Settings.Limits.MaxBytes = 2000000000;
	Settings.Limits.MaxValidationWork = 2000000000;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"),
			FVoxelLandM5Budget::TryCreate(Settings.Limits.MaxBytes, Settings.Limits.MaxValidationWork, Budget)))
	{
		return false;
	}
	FVoxelLandM5CavePlan Plan;
	for (int32 I = 0; I < 3; ++I)
	{
		FVoxelLandM5Chamber Chamber;
		Chamber.bHasId = true;
		Chamber.Id = uint64(I + 1);
		Chamber.CenterCm.X = -300 + 300 * I;
		Chamber.CenterCm.Y = 1450;
		Chamber.CenterCm.Z = 942;
		Chamber.RadiusCm = 150;
		Plan.Chambers.Add(Chamber);
	}
	TArray<FVoxelLandM5RouteAnchorChoice> Anchors;
	for (int32 I = 0; I < 3; ++I)
	{
		FVoxelLandM5RouteAnchorChoice Anchor;
		Anchor.Endpoint.bValid = true;
		Anchor.Endpoint.Id = uint64(I + 1);
		Anchor.Endpoint.AnchorRole = EVoxelLandM5AnchorRole::ChamberFeet;
		Anchor.FeetCm = Plan.Chambers[I].CenterCm;
		Anchor.FeetCm.Z = 850;
		Anchors.Add(Anchor);
	}
	TArray<FVoxelLandM5RouteConnectionChoice> Choices;
	for (int32 I = 0; I < 2; ++I)
	{
		FVoxelLandM5Connection Connection;
		Connection.bHasId = true;
		Connection.Id = uint64(7 + I);
		Connection.Start = Anchors[I].Endpoint;
		Connection.End = Anchors[I + 1].Endpoint;
		Connection.Start.AnchorRole = EVoxelLandM5AnchorRole::None;
		Connection.End.AnchorRole = EVoxelLandM5AnchorRole::None;
		Plan.Connections.Add(Connection);
		FVoxelLandM5Segment Segment;
		Segment.bHasId = true;
		Segment.Id = uint64(9 + I);
		Segment.bHasConnectionId = true;
		Segment.ConnectionId = Connection.Id;
		Segment.StartCm = Plan.Chambers[I].CenterCm;
		Segment.EndCm = Plan.Chambers[I + 1].CenterCm;
		Segment.RadiusCm = 103;
		Plan.Segments.Add(Segment);
		FVoxelLandM5RouteConnectionChoice Choice;
		Choice.bHasConnectionId = true;
		Choice.ConnectionId = Connection.Id;
		Choice.Start = Anchors[I].Endpoint;
		Choice.End = Anchors[I + 1].Endpoint;
		Choice.SegmentIds.Add(Segment.Id);
		Choices.Add(MoveTemp(Choice));
	}
	auto Candidate = FVoxelLandM5MultiRoutesFixtureFactory::Make(Settings, Budget.ToSharedRef(), Plan);
	if (!TestTrue(TEXT("candidate admitted"), Candidate.IsValid()))
	{
		return false;
	}
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Built;
	const auto Build = VoxelLand::M5::RouteBuilder::Build(
		Settings, 11, Plan.Region, Candidate.ToSharedRef(), Anchors, Choices, Budget.ToSharedRef(), Never, Built);
	if (!TestTrue(TEXT("two route candidate built"), Build.Code == EVoxelLandM5ErrorCode::None && Built.IsValid())
		|| !TestTrue(TEXT("two one-leg routes"), Built->GetPlan().Routes.Num() == 2
													 && Built->GetPlan().Routes[0].Legs.Num() == 1
													 && Built->GetPlan().Routes[1].Legs.Num() == 1))
	{
		return false;
	}
	const auto MakeInputs = [&](bool bWall, TArray<FVoxelLandChunkData>& Chunks,
								TArray<FVoxelLandM5MultiRouteSupportSelection>& Selections) {
		Chunks = {M5AllMultiRoutesChunk(-1, bWall), M5AllMultiRoutesChunk(0, bWall)};
		TArray<FVoxelLandM5ChunkFaceSelection> Faces;
		for (const FVoxelLandChunkData& Chunk : Chunks)
		{
			FVoxelLandMeshingScratch Scratch;
			FVoxelLandMeshData Mesh;
			if (VoxelLandMeshing::BuildSurfaceNets(Chunk, 100.f, Scratch, Mesh, Never) != EVoxelLandMeshResult::Success)
			{
				AddError(TEXT("Fixture meshing failed"));
				return false;
			}
			TArray<FVoxelLandM5ValidationTriangle> Triangles;
			if (VoxelLand::M5::MeshAdapter::Adapt(Chunk, 100.f, false, Mesh, 196608, 294912, Never, Triangles).Code
				!= EVoxelLandM5ErrorCode::None)
			{
				AddError(TEXT("Fixture adaptation failed"));
				return false;
			}
			for (int32 Index = 0; Index < Triangles.Num(); ++Index)
			{
				const auto& T = Triangles[Index];
				const auto WithinFloor = [](const FVector3d& V) {
					return V.Z == 850 && V.X >= -650 && V.X <= 650 && V.Y >= 1150 && V.Y <= 1750;
				};
				if (WithinFloor(T.A) && WithinFloor(T.B) && WithinFloor(T.C))
				{
					FVoxelLandM5ChunkFaceSelection Face;
					Face.ChunkCoordinate = Chunk.Key.Coordinate;
					Face.FaceIndex = Index;
					Faces.Add(Face);
				}
			}
		}
		if (!TestTrue(TEXT("bounded support faces on both chunks"), !Faces.IsEmpty() && Faces.Num() <= 256))
		{
			return false;
		}
		for (const FVoxelLandM5Route& Route : Built->GetPlan().Routes)
		{
			for (int32 LegIndex = 0; LegIndex < Route.Legs.Num(); ++LegIndex)
			{
				FVoxelLandM5MultiRouteSupportSelection Selection;
				Selection.RouteId = Route.Id;
				Selection.LegIndex = LegIndex;
				Selection.SupportFaces = Faces;
				Selections.Add(MoveTemp(Selection));
			}
		}
		return true;
	};
	TArray<FVoxelLandChunkData> Chunks;
	TArray<FVoxelLandM5MultiRouteSupportSelection> Selections;
	if (!MakeInputs(false, Chunks, Selections) || !TestEqual(TEXT("two selections"), Selections.Num(), 2))
	{
		return false;
	}
	const auto Check = [&](TConstArrayView<FVoxelLandChunkData> Input,
						   TConstArrayView<FVoxelLandM5MultiRouteSupportSelection> Support,
						   TFunctionRef<bool()> Cancel) {
		return VoxelLand::M5::MultiRouteChecks::CheckAll(Settings, 11, Plan.Region, Built.ToSharedRef(), Input,
			FIntVector(-1, 0, 0), FIntVector(1, 1, 1), 100, false, 0, Support, Budget.ToSharedRef(), Cancel);
	};
	Swap(Selections[0], Selections[1]);
	TestEqual(TEXT("every interval checks across negative X seam regardless of selection order"),
		int32(Check(Chunks, Selections, Never).Code), int32(EVoxelLandM5ErrorCode::None));
	// Determine the deterministic callback counts of the ideal precheck and the
	// first real multi-chunk certification independently. CheckAll additionally
	// polls once at entry, once for each route while counting, and once for each
	// selection in the first lookup. The next poll begins the second lookup.
	int32 IdealPolls = 0;
	const auto CountIdeal = [&IdealPolls] {
		++IdealPolls;
		return false;
	};
	const auto Ideal = VoxelLand::M5::IdealRouteValidation::Validate(
		Settings, 11, Plan.Region, Built.ToSharedRef(), Budget.ToSharedRef(), CountIdeal);
	if (!TestEqual(TEXT("ideal route check succeeds"), int32(Ideal.Code), int32(EVoxelLandM5ErrorCode::None)))
	{
		return false;
	}
	const FVoxelLandM5Route& FirstRoute = Built->GetPlan().Routes[0];
	const FVoxelLandM5MultiRouteSupportSelection* FirstSupport = nullptr;
	for (const auto& Selection : Selections)
	{
		if (Selection.RouteId == FirstRoute.Id && Selection.LegIndex == 0)
		{
			FirstSupport = &Selection;
		}
	}
	if (!TestNotNull(TEXT("first support found"), FirstSupport))
	{
		return false;
	}
	int32 FirstIntervalPolls = 0;
	const auto CountFirstInterval = [&FirstIntervalPolls] {
		++FirstIntervalPolls;
		return false;
	};
	const auto FirstInterval = VoxelLand::M5::MultiChunkRoute::Certify(Chunks, FIntVector(-1, 0, 0),
		FIntVector(1, 1, 1), 100, false, FirstRoute.Legs[0].StartFeetCm, FirstRoute.Legs[0].EndFeetCm,
		Settings.Capsule.RadiusCm, Settings.Capsule.StandingHeightCm, Settings.Capsule.ClearanceMarginCm, 0,
		Settings.Capsule.MaxSlopeDegrees, 90, FirstSupport->SupportFaces, Budget.ToSharedRef(), CountFirstInterval);
	if (!TestEqual(TEXT("first interval independently succeeds"), int32(FirstInterval.Code),
			int32(EVoxelLandM5ErrorCode::None)))
	{
		return false;
	}
	const int32 CancelAt = 1 + IdealPolls + Built->GetPlan().Routes.Num() + Selections.Num() + FirstIntervalPolls + 1;
	int32 CheckPolls = 0;
	const auto CancelAfterFirst = [&CheckPolls, CancelAt] {
		return ++CheckPolls == CancelAt;
	};
	const auto AfterFirst = Check(Chunks, Selections, CancelAfterFirst);
	TestEqual(TEXT("cancellation after first complete interval propagates"), int32(AfterFirst.Code),
		int32(EVoxelLandM5ErrorCode::Cancelled));
	TestTrue(
		TEXT("cancellation occurs at the second route lookup"), AfterFirst.Stage == FName(TEXT("M5.MultiRouteChecks")));
	TestEqual(TEXT("first interval completed before cancellation"), CheckPolls, CancelAt);
	TArray<FVoxelLandM5MultiRouteSupportSelection> Missing = Selections;
	Missing.RemoveAt(1);
	TestEqual(TEXT("missing leg rejected"), int32(Check(Chunks, Missing, Never).Code),
		int32(EVoxelLandM5ErrorCode::InvalidInput));
	TArray<FVoxelLandM5MultiRouteSupportSelection> Duplicate = Selections;
	Duplicate[1].RouteId = Duplicate[0].RouteId;
	Duplicate[1].LegIndex = Duplicate[0].LegIndex;
	TestEqual(TEXT("duplicate selection rejected"), int32(Check(Chunks, Duplicate, Never).Code),
		int32(EVoxelLandM5ErrorCode::InvalidInput));
	TArray<FVoxelLandChunkData> MissingChunk;
	MissingChunk.Add(Chunks[0]);
	TestEqual(TEXT("missing mesh owner rejected"), int32(Check(MissingChunk, Selections, Never).Code),
		int32(EVoxelLandM5ErrorCode::Unproven));
	TArray<FVoxelLandChunkData> Mismatch = Chunks;
	Mismatch[1].Density[0]++;
	TestEqual(TEXT("mismatched halo rejected"), int32(Check(Mismatch, Selections, Never).Code),
		int32(EVoxelLandM5ErrorCode::Unproven));
	TArray<FVoxelLandChunkData> WallChunks;
	TArray<FVoxelLandM5MultiRouteSupportSelection> WallSelections;
	if (!MakeInputs(true, WallChunks, WallSelections))
	{
		return false;
	}
	const auto Wall = Check(WallChunks, WallSelections, Never);
	TestEqual(TEXT("later interval obstacle rejected"), int32(Wall.Code), int32(EVoxelLandM5ErrorCode::Unproven));
	TestTrue(TEXT("obstacle reaches continuous sweep"), Wall.Stage == FName(TEXT("M5.SweptTriangle")));
	const auto Cancel = [] {
		return true;
	};
	TestEqual(TEXT("cancellation propagates"), int32(Check(Chunks, Selections, Cancel).Code),
		int32(EVoxelLandM5ErrorCode::Cancelled));
	return true;
}
#endif
