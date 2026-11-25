#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Data/Meshing/VoxelLandMeshData.h"
#include "Data/Meshing/VoxelLandMeshingScratch.h"
#include "Generation/Caves/Field/VoxelLandCaveMeshAdapter.h"
#include "Generation/Meshing/VoxelLandMeshing.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags M5MeshAdapterFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FVoxelLandChunkData M5AdapterPlane(int32 ChunkX)
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
				Chunk.Density[(X + 1) + 35 * ((Y + 1) + 35 * (Z + 1))] = Z <= 0 ? -100 : 100;
			}
		}
	}
	return Chunk;
}

bool M5AdapterContains(const FVoxelLandM5ValidationTriangle& T, const FVector3d& P)
{
	return T.A == P || T.B == P || T.C == P;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5MeshAdapterPlaneTest, "VoxelLand.M5.MeshAdapter.Planes", M5MeshAdapterFlags)
bool FVoxelLandM5MeshAdapterPlaneTest::RunTest(const FString& Parameters)
{
	TArray<FVoxelLandM5ValidationTriangle> Converted[2];
	for (int32 N = 0; N < 2; ++N)
	{
		const FVoxelLandChunkData Chunk = M5AdapterPlane(N - 1);
		FVoxelLandMeshingScratch Scratch;
		FVoxelLandMeshData Mesh;
		if (!TestEqual(TEXT("actual mesh succeeds"),
				int32(VoxelLandMeshing::BuildSurfaceNets(Chunk, 100.f, Scratch, Mesh, [] { return false; })),
				int32(EVoxelLandMeshResult::Success)))
		{
			return false;
		}
		const auto Result = VoxelLand::M5::MeshAdapter::Adapt(
			Chunk, 100.f, false, Mesh, 2048, 100352, [] { return false; }, Converted[N]);
		if (!TestEqual(TEXT("adapter succeeds"), int32(Result.Code), int32(EVoxelLandM5ErrorCode::None)))
		{
			return false;
		}
		TestEqual(TEXT("plane has two triangles per owning vertical edge"), Converted[N].Num(), 2048);
		const FVector Origin = VoxelLandMeshCoordinates::RenderOrigin(Chunk.Key.Coordinate, 100.f);
		for (int32 I = 0; I < Converted[N].Num(); ++I)
		{
			const auto& Triangle = Converted[N][I];
			TestEqual(TEXT("source order retained"), Triangle.SourceTriangle, I);
			const FVector3d Points[3] = {Triangle.A, Triangle.B, Triangle.C};
			for (int32 V = 0; V < 3; ++V)
			{
				const FVector3d Expected = FVector3d(Mesh.Vertices[Mesh.Indices[I * 3 + V]].Position) + Origin;
				TestTrue(TEXT("indexed winding retained"), Points[V] == Expected);
				TestEqual(TEXT("analytic plane height"), Points[V].Z, 50.0);
				TestTrue(TEXT("outward box contains vertex"),
					Points[V].X >= Triangle.Bounds.Min.X && Points[V].X <= Triangle.Bounds.Max.X
						&& Points[V].Y >= Triangle.Bounds.Min.Y && Points[V].Y <= Triangle.Bounds.Max.Y
						&& Points[V].Z >= Triangle.Bounds.Min.Z && Points[V].Z <= Triangle.Bounds.Max.Z);
			}
		}
	}
	// The plane produces a shared ghost-cell vertex on both sides of X=0.
	const FVector3d Shared(-50, 50, 50);
	for (int32 N = 0; N < 2; ++N)
	{
		bool bFound = false;
		for (const auto& Triangle : Converted[N])
		{
			bFound |= M5AdapterContains(Triangle, Shared);
		}
		TestTrue(TEXT("negative/zero neighboring owners retain shared vertex"), bFound);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM5MeshAdapterFailureTest, "VoxelLand.M5.MeshAdapter.Failures", M5MeshAdapterFlags)
bool FVoxelLandM5MeshAdapterFailureTest::RunTest(const FString& Parameters)
{
	const FVoxelLandChunkData Chunk = M5AdapterPlane(0);
	FVoxelLandMeshingScratch Scratch;
	FVoxelLandMeshData Mesh;
	if (!TestEqual(TEXT("actual mesh succeeds"),
			int32(VoxelLandMeshing::BuildSurfaceNets(Chunk, 100.f, Scratch, Mesh, [] { return false; })),
			int32(EVoxelLandMeshResult::Success)))
	{
		return false;
	}
	TArray<FVoxelLandM5ValidationTriangle> Output;
	Output.AddDefaulted();
	Output[0].SourceTriangle = 71;
	const auto Check = [this, &Chunk, &Output](const FVoxelLandMeshData& Input, int32 Limit, int64 Work,
						   TFunctionRef<bool()> Cancel, EVoxelLandM5ErrorCode Expected) {
		const auto D = VoxelLand::M5::MeshAdapter::Adapt(Chunk, 100.f, false, Input, Limit, Work, Cancel, Output);
		TestEqual(TEXT("failure code"), int32(D.Code), int32(Expected));
		TestEqual(TEXT("previous output retained"), Output.Num(), 1);
		if (Output.Num() == 1)
		{
			TestEqual(TEXT("previous sentinel retained"), Output[0].SourceTriangle, 71);
		}
	};
	FVoxelLandMeshData Missing = Mesh;
	Missing.Indices.RemoveAt(Missing.Indices.Num() - 3, 3);
	Check(Missing, 2048, 100352, [] { return false; }, EVoxelLandM5ErrorCode::Unproven);
	FVoxelLandMeshData BadIndex = Mesh;
	BadIndex.Indices[0] = uint32(BadIndex.Vertices.Num());
	Check(BadIndex, 2048, 100352, [] { return false; }, EVoxelLandM5ErrorCode::InvalidInput);
	Check(Mesh, 2047, 100352, [] { return false; }, EVoxelLandM5ErrorCode::BudgetExceeded);
	Check(Mesh, 2048, 1, [] { return false; }, EVoxelLandM5ErrorCode::BudgetExceeded);
	Check(Mesh, 2048, 100352, [] { return true; }, EVoxelLandM5ErrorCode::Cancelled);
	// A real M4 mesh may have no faces; the entire sample footprint still has
	// to be inside the M5 numerical domain before that emptiness is accepted.
	FVoxelLandChunkData Far;
	Far.Key.Coordinate = FIntVector(1024, 0, 0);
	Far.Density.Init(-1, FVoxelLandChunkData::SampleCount);
	FVoxelLandMeshData Empty;
	const auto FarResult =
		VoxelLand::M5::MeshAdapter::Adapt(Far, 1000.f, false, Empty, 0, 100352, [] { return false; }, Output);
	TestEqual(
		TEXT("empty out-of-domain halo rejected"), int32(FarResult.Code), int32(EVoxelLandM5ErrorCode::OutOfDomain));
	TestEqual(TEXT("out-of-domain retains output"), Output.Num(), 1);
	if (Output.Num() == 1)
	{
		TestEqual(TEXT("out-of-domain retains sentinel"), Output[0].SourceTriangle, 71);
	}

	// Deliberately synthetic indexed meshes exercise the predicate only; the
	// real adapter caller must separately establish actual-mesh provenance.
	FVoxelLandMeshData PredicateMesh;
	PredicateMesh.Vertices.SetNum(3);
	PredicateMesh.Vertices[0].Position = FVector3f(0, 0, 50);
	PredicateMesh.Vertices[1].Position = FVector3f(1, 1, 50);
	PredicateMesh.Vertices[2].Position = FVector3f(2, 2, 50);
	for (int32 I = 0; I < 2048; ++I)
	{
		PredicateMesh.Indices.Add(0);
		PredicateMesh.Indices.Add(1);
		PredicateMesh.Indices.Add(2);
	}
	Check(PredicateMesh, 2048, 100352, [] { return false; }, EVoxelLandM5ErrorCode::Unproven);
	PredicateMesh.Vertices[2].Position.Y = 2.000001f;
	TArray<FVoxelLandM5ValidationTriangle> NearResult;
	const auto Near = VoxelLand::M5::MeshAdapter::Adapt(
		Chunk, 100.f, false, PredicateMesh, 2048, 100352, [] { return false; }, NearResult);
	TestEqual(TEXT("nonzero near-collinear triangle is proven"), int32(Near.Code), int32(EVoxelLandM5ErrorCode::None));
	TestEqual(TEXT("all near-collinear triangle indices retained"), NearResult.Num(), 2048);
	return true;
}
#endif
