#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Structs/VoxelLandChunkData.h"
#include "Data/Structs/VoxelLandMeshData.h"
#include "Data/Structs/VoxelLandMeshingScratch.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5MeshAdapter.h"
#include "Generation/VoxelLandM5SingleChunkRoute.h"
#include "Generation/VoxelLandMeshing.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5SingleRouteFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandChunkData M5SingleRouteFlatChunk()
{
	FVoxelLandChunkData Chunk;
	Chunk.Density.SetNumUninitialized(FVoxelLandChunkData::SampleCount);
	for (int32 Z = -1; Z <= 33; ++Z)
	{
		for (int32 Y = -1; Y <= 33; ++Y)
		{
			for (int32 X = -1; X <= 33; ++X)
			{
				Chunk.Density[(X + 1) + 35 * ((Y + 1) + 35 * (Z + 1))] = Z <= 8 ? -100 : 100;
			}
		}
	}
	return Chunk;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5SingleChunkRouteTest, "VoxelLand.M5.SingleChunkRoute.ActualMesh", M5SingleRouteFlags)
bool FVoxelLandM5SingleChunkRouteTest::RunTest(const FString& Parameters)
{
	const auto NeverCancel = [] {
		return false;
	};
	const FVoxelLandChunkData Chunk = M5SingleRouteFlatChunk();
	FVoxelLandMeshingScratch Scratch;
	FVoxelLandMeshData Mesh;
	if (!TestEqual(TEXT("analytic plane meshes"),
			int32(VoxelLandMeshing::BuildSurfaceNets(Chunk, 100.f, Scratch, Mesh, NeverCancel)),
			int32(EVoxelLandMeshResult::Success)))
	{
		return false;
	}
	TArray<FVoxelLandM5ValidationTriangle> Adapted;
	const auto Adapt = VoxelLand::M5::MeshAdapter::Adapt(Chunk, 100.f, false, Mesh, 2048, 100352, NeverCancel, Adapted);
	if (!TestEqual(TEXT("plane adapts"), int32(Adapt.Code), int32(EVoxelLandM5ErrorCode::None)))
	{
		return false;
	}
	// The independently known plane is Z=850 cm. Select a 600 x 600 cm
	// region around the route from the actual indexed mesh; every other face
	// remains an obstacle. The capsule footprint is strictly inside this patch.
	TArray<int32> Support;
	for (int32 Index = 0; Index < Adapted.Num(); ++Index)
	{
		const auto& T = Adapted[Index];
		if (T.A.X >= 1150 && T.A.X <= 1750 && T.B.X >= 1150 && T.B.X <= 1750 && T.C.X >= 1150 && T.C.X <= 1750
			&& T.A.Y >= 1150 && T.A.Y <= 1750 && T.B.Y >= 1150 && T.B.Y <= 1750 && T.C.Y >= 1150 && T.C.Y <= 1750)
		{
			Support.Add(Index);
		}
	}
	if (!TestTrue(TEXT("bounded plane support selected"), Support.Num() > 0 && Support.Num() <= 256))
	{
		return false;
	}
	FVoxelLandM5PointCm A, B;
	A.X = 1400;
	A.Y = 1450;
	A.Z = 850;
	B.X = 1500;
	B.Y = 1450;
	B.Z = 850;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget"), FVoxelLandM5Budget::TryCreate(512ll * 1024 * 1024, 1000000000, Budget)))
	{
		return false;
	}
	const auto Check = [&](const FVoxelLandM5PointCm& Start, TConstArrayView<int32> Faces) {
		return VoxelLand::M5::SingleChunkRoute::Certify(
			Chunk, 100, false, Start, B, 35, 185, 10, 0, 1, 1, Faces, Budget.ToSharedRef(), NeverCancel)
			.Code;
	};
	TestEqual(TEXT("actual flat CPU mesh supports complete interval"), int32(Check(A, Support)),
		int32(EVoxelLandM5ErrorCode::None));
	TArray<int32> Duplicate = Support;
	Duplicate.Add(Support[0]);
	TestEqual(
		TEXT("duplicate support index fails"), int32(Check(A, Duplicate)), int32(EVoxelLandM5ErrorCode::InvalidInput));
	TArray<int32> Missing = Support;
	Missing.RemoveAt(0, Missing.Num() - 1);
	TestEqual(TEXT("incomplete patch cannot prove support"), int32(Check(A, Missing)),
		int32(EVoxelLandM5ErrorCode::Unproven));
	FVoxelLandM5PointCm Outside = A;
	Outside.X = 100;
	TestEqual(TEXT("neighbor-owned geometry cannot be omitted"), int32(Check(Outside, Support)),
		int32(EVoxelLandM5ErrorCode::Unproven));
	const auto Cancel = [] {
		return true;
	};
	TestEqual(TEXT("cancellation"),
		int32(VoxelLand::M5::SingleChunkRoute::Certify(
			Chunk, 100, false, A, B, 35, 185, 10, 0, 1, 1, Support, Budget.ToSharedRef(), Cancel)
				.Code),
		int32(EVoxelLandM5ErrorCode::Cancelled));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5SingleChunkRouteObstacleTest, "VoxelLand.M5.SingleChunkRoute.ActualObstacles", M5SingleRouteFlags)
bool FVoxelLandM5SingleChunkRouteObstacleTest::RunTest(const FString& Parameters)
{
	const auto NeverCancel = [] {
		return false;
	};
	FVoxelLandM5PointCm Start, End;
	Start.X = 1400;
	Start.Y = 1450;
	Start.Z = 850;
	End.X = 1600;
	End.Y = 1450;
	End.Z = 850;
	for (int32 Scenario = 0; Scenario < 2; ++Scenario)
	{
		FVoxelLandChunkData Chunk = M5SingleRouteFlatChunk();
		for (int32 Z = -1; Z <= 33; ++Z)
		{
			for (int32 Y = -1; Y <= 33; ++Y)
			{
				for (int32 X = -1; X <= 33; ++X)
				{
					// Independently defined solids: a thin floating slab intersects
					// the middle of the route (but neither endpoint), or a ceiling
					// begins 100 cm above the 850 cm floor.
					if ((Scenario == 0 && X == 15 && Z >= 10 && Z <= 11) || (Scenario == 1 && Z >= 10))
					{
						Chunk.Density[(X + 1) + 35 * ((Y + 1) + 35 * (Z + 1))] = -100;
					}
				}
			}
		}
		FVoxelLandMeshingScratch Scratch;
		FVoxelLandMeshData Mesh;
		if (!TestEqual(TEXT("obstacle field meshes"),
				int32(VoxelLandMeshing::BuildSurfaceNets(Chunk, 100.f, Scratch, Mesh, NeverCancel)),
				int32(EVoxelLandMeshResult::Success)))
		{
			return false;
		}
		TArray<FVoxelLandM5ValidationTriangle> Triangles;
		const auto Adapt =
			VoxelLand::M5::MeshAdapter::Adapt(Chunk, 100.f, false, Mesh, 196608, 294912, NeverCancel, Triangles);
		if (!TestEqual(TEXT("obstacle mesh adapts"), int32(Adapt.Code), int32(EVoxelLandM5ErrorCode::None)))
		{
			return false;
		}
		TArray<int32> Support;
		int32 NonFloorIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Triangles.Num(); ++Index)
		{
			const auto& T = Triangles[Index];
			const bool bFloor = T.A.Z == 850.0 && T.B.Z == 850.0 && T.C.Z == 850.0;
			if (!bFloor && NonFloorIndex == INDEX_NONE)
			{
				NonFloorIndex = Index;
			}
			if (bFloor && T.A.X >= 1150 && T.A.X <= 1850 && T.B.X >= 1150 && T.B.X <= 1850 && T.C.X >= 1150
				&& T.C.X <= 1850 && T.A.Y >= 1150 && T.A.Y <= 1750 && T.B.Y >= 1150 && T.B.Y <= 1750 && T.C.Y >= 1150
				&& T.C.Y <= 1750)
			{
				Support.Add(Index);
			}
		}
		if (!TestTrue(TEXT("bounded floor and obstacle faces exist"),
				!Support.IsEmpty() && Support.Num() < 256 && NonFloorIndex != INDEX_NONE))
		{
			return false;
		}
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
		if (!TestTrue(TEXT("obstacle budget"), FVoxelLandM5Budget::TryCreate(512ll * 1024 * 1024, 1000000000, Budget)))
		{
			return false;
		}
		const auto Check = [&](TConstArrayView<int32> Faces) {
			return VoxelLand::M5::SingleChunkRoute::Certify(
				Chunk, 100, false, Start, End, 35, 185, 10, 0, 1, 1, Faces, Budget.ToSharedRef(), NeverCancel)
				.Code;
		};
		TestEqual(Scenario == 0 ? TEXT("midpoint slab rejects continuous route") : TEXT("low ceiling rejects route"),
			int32(Check(Support)), int32(EVoxelLandM5ErrorCode::Unproven));
		TArray<int32> MaliciousSupport = Support;
		MaliciousSupport.Add(NonFloorIndex);
		TestEqual(TEXT("obstacle cannot be exempted as support"), int32(Check(MaliciousSupport)),
			int32(EVoxelLandM5ErrorCode::Unproven));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5SingleChunkRouteBudgetTest, "VoxelLand.M5.SingleChunkRoute.AfterMeshing", M5SingleRouteFlags)
bool FVoxelLandM5SingleChunkRouteBudgetTest::RunTest(const FString& Parameters)
{
	const FVoxelLandChunkData Chunk = M5SingleRouteFlatChunk();
	FVoxelLandM5PointCm A, B;
	A.X = 1400;
	A.Y = 1450;
	A.Z = 850;
	B.X = 1500;
	B.Y = 1450;
	B.Z = 850;
	const int32 Face[] = {0};
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("limited budget"), FVoxelLandM5Budget::TryCreate(512ll * 1024 * 1024, 4000000, Budget)))
	{
		return false;
	}
	int32 Polls = 0;
	const auto CountOnly = [&Polls] {
		++Polls;
		return false;
	};
	const auto Exhausted = VoxelLand::M5::SingleChunkRoute::Certify(
		Chunk, 100, false, A, B, 35, 185, 10, 0, 1, 1, Face, Budget.ToSharedRef(), CountOnly);
	TestEqual(TEXT("post-mesh conversion cannot exceed shared work"), int32(Exhausted.Code),
		int32(EVoxelLandM5ErrorCode::BudgetExceeded));
	TestTrue(TEXT("failure occurs after production mesher"), Polls > 67);
	int64 Live = -1, Peak = -1, Work = -1;
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("scratch lease released"), Live, int64(0));
	TestEqual(TEXT("precharged work retained"), Work, int64(4000000));
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> CancelBudget;
	if (!TestTrue(
			TEXT("cancellation budget"), FVoxelLandM5Budget::TryCreate(512ll * 1024 * 1024, 1000000000, CancelBudget)))
	{
		return false;
	}
	Polls = 0;
	const auto CancelAfterMesh = [&Polls] {
		return ++Polls == 70;
	};
	const auto Cancelled = VoxelLand::M5::SingleChunkRoute::Certify(
		Chunk, 100, false, A, B, 35, 185, 10, 0, 1, 1, Face, CancelBudget.ToSharedRef(), CancelAfterMesh);
	TestEqual(TEXT("cancellation while adapting actual mesh"), int32(Cancelled.Code),
		int32(EVoxelLandM5ErrorCode::Cancelled));
	TestEqual(TEXT("deterministic post-meshing cancellation"), Polls, 70);
	CancelBudget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("cancelled scratch lease released"), Live, int64(0));
	return true;
}
#endif
