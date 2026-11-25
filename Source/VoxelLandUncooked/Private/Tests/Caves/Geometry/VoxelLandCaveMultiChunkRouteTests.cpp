#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Data/Meshing/VoxelLandMeshData.h"
#include "Data/Meshing/VoxelLandMeshingScratch.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Field/VoxelLandCaveMeshAdapter.h"
#include "Generation/Caves/Geometry/VoxelLandCaveMultiChunkRoute.h"
#include "Generation/Meshing/VoxelLandMeshing.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5MultiRouteFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandChunkData M5MultiRoutePlane(int32 ChunkX)
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
				Chunk.Density[(X + 1) + 35 * ((Y + 1) + 35 * (Z + 1))] = Z <= 8 ? -100 : 100;
			}
		}
	}
	return Chunk;
}

FVoxelLandChunkData M5MultiRouteWorldPlane(const FIntVector& Coordinate, int32 SolidThroughZ, int32 Scenario)
{
	FVoxelLandChunkData Chunk;
	Chunk.Key.Coordinate = Coordinate;
	Chunk.Density.SetNumUninitialized(FVoxelLandChunkData::SampleCount);
	for (int32 Z = -1; Z <= 33; ++Z)
	{
		for (int32 Y = -1; Y <= 33; ++Y)
		{
			for (int32 X = -1; X <= 33; ++X)
			{
				const int64 GlobalY = int64(Coordinate.Y) * 32 + Y;
				const int64 GlobalZ = int64(Coordinate.Z) * 32 + Z;
				const bool bObstacle = (Scenario == 1 && GlobalY == 0 && GlobalZ >= 10 && GlobalZ <= 11)
									|| (Scenario == 2 && GlobalZ >= 10);
				Chunk.Density[(X + 1) + 35 * ((Y + 1) + 35 * (Z + 1))] =
					GlobalZ <= SolidThroughZ || bObstacle ? -100 : 100;
			}
		}
	}
	return Chunk;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5MultiChunkRouteTest, "VoxelLand.M5.MultiChunkRoute.CrossBoundary", M5MultiRouteFlags)
bool FVoxelLandM5MultiChunkRouteTest::RunTest(const FString& Parameters)
{
	const auto NeverCancel = [] {
		return false;
	};
	TArray<FVoxelLandChunkData> Chunks;
	Chunks.Add(M5MultiRoutePlane(-1));
	Chunks.Add(M5MultiRoutePlane(0));
	TArray<FVoxelLandM5ChunkFaceSelection> Faces;
	for (const auto& Chunk : Chunks)
	{
		FVoxelLandMeshingScratch Scratch;
		FVoxelLandMeshData Mesh;
		if (!TestEqual(TEXT("plane mesh generated"),
				int32(VoxelLandMeshing::BuildSurfaceNets(Chunk, 100.f, Scratch, Mesh, NeverCancel)),
				int32(EVoxelLandMeshResult::Success)))
		{
			return false;
		}
		TArray<FVoxelLandM5ValidationTriangle> Triangles;
		const auto Adapted =
			VoxelLand::M5::MeshAdapter::Adapt(Chunk, 100.f, false, Mesh, 2048, 100352, NeverCancel, Triangles);
		if (!TestEqual(TEXT("plane mesh adapted"), int32(Adapted.Code), int32(EVoxelLandM5ErrorCode::None)))
		{
			return false;
		}
		for (int32 I = 0; I < Triangles.Num(); ++I)
		{
			const auto& T = Triangles[I];
			if (T.A.X >= -450 && T.B.X >= -450 && T.C.X >= -450 && T.A.X <= 450 && T.B.X <= 450 && T.C.X <= 450
				&& T.A.Y >= 1150 && T.B.Y >= 1150 && T.C.Y >= 1150 && T.A.Y <= 1750 && T.B.Y <= 1750 && T.C.Y <= 1750)
			{
				FVoxelLandM5ChunkFaceSelection Face;
				Face.ChunkCoordinate = Chunk.Key.Coordinate;
				Face.FaceIndex = I;
				Faces.Add(Face);
			}
		}
	}
	if (!TestTrue(TEXT("support spans both chunks"), Faces.Num() > 0 && Faces.Num() <= 256))
	{
		return false;
	}
	FVoxelLandM5PointCm Start, End;
	Start.X = -150;
	Start.Y = 1450;
	Start.Z = 850;
	End.X = 150;
	End.Y = 1450;
	End.Z = 850;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("budget created"), FVoxelLandM5Budget::TryCreate(2000000000, 2000000000, Budget)))
	{
		return false;
	}
	const auto Check = [&](TConstArrayView<FVoxelLandChunkData> Set,
						   TConstArrayView<FVoxelLandM5ChunkFaceSelection> Selected, const FVoxelLandM5PointCm& A) {
		return VoxelLand::M5::MultiChunkRoute::Certify(Set, FIntVector(-1, 0, 0), FIntVector(1, 1, 1), 100, false, A,
			End, 35, 185, 0, 0, 1, 1, Selected, Budget.ToSharedRef(), NeverCancel)
			.Code;
	};
	TestEqual(TEXT("continuous route crosses negative-to-positive chunk seam"), int32(Check(Chunks, Faces, Start)),
		int32(EVoxelLandM5ErrorCode::None));
	const auto MarginResult = VoxelLand::M5::MultiChunkRoute::Certify(Chunks, FIntVector(-1, 0, 0), FIntVector(1, 1, 1),
		100, false, Start, End, 35, 185, 10, 0, 1, 1, Faces, Budget.ToSharedRef(), NeverCancel);
	TestEqual(TEXT("positive margin across supported chunk seam"), int32(MarginResult.Code),
		int32(EVoxelLandM5ErrorCode::None));
	TArray<FVoxelLandChunkData> Missing;
	Missing.Add(Chunks[0]);
	TestEqual(
		TEXT("missing neighbor rejected"), int32(Check(Missing, Faces, Start)), int32(EVoxelLandM5ErrorCode::Unproven));
	TArray<FVoxelLandChunkData> Mismatch = Chunks;
	Mismatch[1].Density[0] += 1;
	TestEqual(TEXT("different overlap rejected"), int32(Check(Mismatch, Faces, Start)),
		int32(EVoxelLandM5ErrorCode::Unproven));
	TArray<FVoxelLandM5ChunkFaceSelection> Duplicate = Faces;
	Duplicate.Add(Faces[0]);
	TestEqual(TEXT("duplicate support rejected"), int32(Check(Chunks, Duplicate, Start)),
		int32(EVoxelLandM5ErrorCode::InvalidInput));
	FVoxelLandM5PointCm Outside = Start;
	Outside.X = -3100;
	TestEqual(TEXT("outside owner margin rejected"), int32(Check(Chunks, Faces, Outside)),
		int32(EVoxelLandM5ErrorCode::Unproven));
	const auto Cancel = [] {
		return true;
	};
	TestEqual(TEXT("cancelled"),
		int32(VoxelLand::M5::MultiChunkRoute::Certify(Chunks, FIntVector(-1, 0, 0), FIntVector(1, 1, 1), 100, false,
			Start, End, 35, 185, 0, 0, 1, 1, Faces, Budget.ToSharedRef(), Cancel)
				.Code),
		int32(EVoxelLandM5ErrorCode::Cancelled));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5MultiChunkRouteOtherAxesTest, "VoxelLand.M5.MultiChunkRoute.YZAndObstacles", M5MultiRouteFlags)
bool FVoxelLandM5MultiChunkRouteOtherAxesTest::RunTest(const FString& Parameters)
{
	const auto NeverCancel = [] {
		return false;
	};
	// Scenario 0 traverses the negative/positive Y seam. 3 keeps a horizontal
	// floor at -50 cm while the upright capsule straddles the Z=0 seam.
	// Scenarios 1 and 2 add a Y-seam midpoint slab or a low ceiling.
	for (int32 Scenario = 0; Scenario < 4; ++Scenario)
	{
		const bool bZSeam = Scenario == 3;
		TArray<FVoxelLandChunkData> Chunks;
		Chunks.Add(
			M5MultiRouteWorldPlane(bZSeam ? FIntVector(0, 0, -1) : FIntVector(0, -1, 0), bZSeam ? -1 : 8, Scenario));
		Chunks.Add(M5MultiRouteWorldPlane(FIntVector(0, 0, 0), bZSeam ? -1 : 8, Scenario));
		const FIntVector Min = bZSeam ? FIntVector(0, 0, -1) : FIntVector(0, -1, 0);
		const FIntVector Max(1, 1, 1);
		FVoxelLandM5PointCm Start, End;
		Start.X = bZSeam ? 1400 : 1450;
		Start.Y = bZSeam ? 1450 : -150;
		Start.Z = bZSeam ? -50 : 850;
		End.X = bZSeam ? 1500 : 1450;
		End.Y = bZSeam ? 1450 : 150;
		End.Z = Start.Z;
		TArray<FVoxelLandM5ChunkFaceSelection> Faces;
		for (const auto& Chunk : Chunks)
		{
			FVoxelLandMeshingScratch Scratch;
			FVoxelLandMeshData Mesh;
			if (!TestEqual(TEXT("axis fixture meshes"),
					int32(VoxelLandMeshing::BuildSurfaceNets(Chunk, 100.f, Scratch, Mesh, NeverCancel)),
					int32(EVoxelLandMeshResult::Success)))
			{
				return false;
			}
			TArray<FVoxelLandM5ValidationTriangle> Triangles;
			const auto Adapt =
				VoxelLand::M5::MeshAdapter::Adapt(Chunk, 100.f, false, Mesh, 196608, 294912, NeverCancel, Triangles);
			if (!TestEqual(TEXT("axis fixture adapts"), int32(Adapt.Code), int32(EVoxelLandM5ErrorCode::None)))
			{
				return false;
			}
			for (int32 I = 0; I < Triangles.Num(); ++I)
			{
				const auto& T = Triangles[I];
				const auto InSupport = [&](const FVector3d& V) {
					return V.Z == double(Start.Z) && V.X >= 1150 && V.X <= 1750 && V.Y >= (bZSeam ? 1150 : -450)
						&& V.Y <= (bZSeam ? 1750 : 450);
				};
				if (InSupport(T.A) && InSupport(T.B) && InSupport(T.C))
				{
					FVoxelLandM5ChunkFaceSelection Face;
					Face.ChunkCoordinate = Chunk.Key.Coordinate;
					Face.FaceIndex = I;
					Faces.Add(Face);
				}
			}
		}
		if (!TestTrue(TEXT("bounded floor support exists"), !Faces.IsEmpty() && Faces.Num() <= 256))
		{
			return false;
		}
		TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
		if (!TestTrue(TEXT("axis budget"), FVoxelLandM5Budget::TryCreate(2000000000, 2000000000, Budget)))
		{
			return false;
		}
		const auto Result = VoxelLand::M5::MultiChunkRoute::Certify(
			Chunks, Min, Max, 100, false, Start, End, 35, 185, 0, 0, 1, 1, Faces, Budget.ToSharedRef(), NeverCancel);
		TestEqual(TEXT("actual field route outcome"), int32(Result.Code),
			int32(Scenario == 1 || Scenario == 2 ? EVoxelLandM5ErrorCode::Unproven : EVoxelLandM5ErrorCode::None));
		if (Scenario == 1 || Scenario == 2)
		{
			TestTrue(TEXT("obstacle reaches continuous triangle separation"),
				Result.Stage == FName(TEXT("M5.SweptTriangle")));
			TestEqual(TEXT("triangle separation rejects obstacle"), Result.Message,
				FString(TEXT("Triangle separation could not be certified")));
		}
		if (Scenario == 0)
		{
			TArray<FVoxelLandM5ChunkFaceSelection> Bad = Faces;
			Bad[0].FaceIndex = -1;
			TestEqual(TEXT("malformed support face rejected"),
				int32(VoxelLand::M5::MultiChunkRoute::Certify(Chunks, Min, Max, 100, false, Start, End, 35, 185, 0, 0,
					1, 1, Bad, Budget.ToSharedRef(), NeverCancel)
						.Code),
				int32(EVoxelLandM5ErrorCode::InvalidInput));
			Bad = Faces;
			Bad[0].ChunkCoordinate = FIntVector(5, 5, 5);
			TestEqual(TEXT("nonexistent support owner rejected"),
				int32(VoxelLand::M5::MultiChunkRoute::Certify(Chunks, Min, Max, 100, false, Start, End, 35, 185, 0, 0,
					1, 1, Bad, Budget.ToSharedRef(), NeverCancel)
						.Code),
				int32(EVoxelLandM5ErrorCode::InvalidInput));
			TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> CancelBudget;
			if (!TestTrue(TEXT("cancel budget"), FVoxelLandM5Budget::TryCreate(2000000000, 2000000000, CancelBudget)))
			{
				return false;
			}
			// A fixed-mesh conversion charges 32 work units only after the first
			// selected face's source chunk has completed production meshing.
			const int64 InitialWork = 24017 + 2 * int64(4000000) + 2 * int64(196608) * (Faces.Num() + 1)
									+ int64(Faces.Num()) * (Faces.Num() + 2);
			const auto CancelAfterMesh = [&] {
				int64 Live = 0, Peak = 0, Work = 0;
				CancelBudget->GetUsage(Live, Peak, Work);
				return Work > InitialWork;
			};
			TestEqual(TEXT("cancel after a chunk has meshed"),
				int32(VoxelLand::M5::MultiChunkRoute::Certify(Chunks, Min, Max, 100, false, Start, End, 35, 185, 0, 0,
					1, 1, Faces, CancelBudget.ToSharedRef(), CancelAfterMesh)
						.Code),
				int32(EVoxelLandM5ErrorCode::Cancelled));
			int64 Live = -1, Peak = -1, Work = -1;
			CancelBudget->GetUsage(Live, Peak, Work);
			TestEqual(TEXT("cancel releases mesh reservation"), Live, int64(0));
			TestTrue(TEXT("conversion was reached after meshing"), Work > InitialWork);
		}
	}
	return true;
}
#endif
