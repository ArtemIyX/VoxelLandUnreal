#if WITH_DEV_AUTOMATION_TESTS

#include "Generation/Meshing/VoxelLandChaosMeshing.h"

#include "Async/Async.h"
#include "Data/Meshing/VoxelLandChaosMesh.h"
#include "Generation/Meshing/VoxelLandMeshing.h"
#include "Misc/AutomationTest.h"

class FVoxelLandChaosMeshFixture final
{
public:
	static constexpr float VoxelSizeCm = 10.f;
	static constexpr EAutomationTestFlags Flags =
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	static bool NeverCancel() { return false; }

	static bool AlwaysCancel() { return true; }

	static FVoxelLandChunkData MakeSlope(FIntVector Coordinate);
	static bool GetProjectedArea(const FVoxelLandChaosMesh& Mesh, double& OutArea);
	static TSet<FIntPoint> GetSeamEdges(const FVoxelLandChaosMesh& Mesh, const FVector& Origin, double GlobalX);
	static TSet<FVector> GetSeamVertices(const FVoxelLandChaosMesh& Mesh, const FVector& Origin, double GlobalX);
	static bool CheckReductions(FAutomationTestBase& Test);
};

FVoxelLandChunkData FVoxelLandChaosMeshFixture::MakeSlope(FIntVector Coordinate)
{
	constexpr int32 Cells = FVoxelLandChunkData::Cells;
	FVoxelLandChunkData Chunk;
	Chunk.Key.Coordinate = Coordinate;
	Chunk.Density.Reserve(FVoxelLandChunkData::SampleCount);
	for (int32 Z = -1; Z <= Cells + 1; ++Z)
	{
		for (int32 Y = -1; Y <= Cells + 1; ++Y)
		{
			const int32 GY = Coordinate.Y * Cells + Y;
			const int32 GZ = Coordinate.Z * Cells + Z;
			const int16 Density = static_cast<int16>((8 * GZ - GY - 97) * 64);
			for (int32 X = -1; X <= Cells + 1; ++X)
			{
				Chunk.Density.Add(Density);
			}
		}
	}
	return Chunk;
}

bool FVoxelLandChaosMeshFixture::GetProjectedArea(const FVoxelLandChaosMesh& Mesh, double& OutArea)
{
	if (Mesh.Indices.IsEmpty() || Mesh.Indices.Num() % 3 != 0 || !Mesh.Bounds.IsValid)
	{
		return false;
	}
	double Area = 0.0;
	for (int32 Index = 0; Index < Mesh.Indices.Num(); Index += 3)
	{
		const int32 A = static_cast<int32>(Mesh.Indices[Index]);
		const int32 B = static_cast<int32>(Mesh.Indices[Index + 1]);
		const int32 C = static_cast<int32>(Mesh.Indices[Index + 2]);
		if (!Mesh.Vertices.IsValidIndex(A) || !Mesh.Vertices.IsValidIndex(B) || !Mesh.Vertices.IsValidIndex(C))
		{
			return false;
		}
		const FVector3d First = FVector3d(Mesh.Vertices[B]) - FVector3d(Mesh.Vertices[A]);
		const FVector3d Second = FVector3d(Mesh.Vertices[C]) - FVector3d(Mesh.Vertices[A]);
		const double TwiceArea = FVector3d::CrossProduct(First, Second).Z;
		if (TwiceArea <= 0.0)
		{
			return false;
		}
		Area += TwiceArea * 0.5;
	}
	OutArea = Area;
	return true;
}

TSet<FIntPoint> FVoxelLandChaosMeshFixture::GetSeamEdges(
	const FVoxelLandChaosMesh& Mesh, const FVector& Origin, double GlobalX)
{
	TSet<FIntPoint> Edges;
	for (int32 Index = 0; Index < Mesh.Indices.Num(); Index += 3)
	{
		for (int32 Edge = 0; Edge < 3; ++Edge)
		{
			const FVector A = Origin + FVector(Mesh.Vertices[Mesh.Indices[Index + Edge]]);
			const FVector B = Origin + FVector(Mesh.Vertices[Mesh.Indices[Index + (Edge + 1) % 3]]);
			if (A.X == GlobalX && B.X == GlobalX)
			{
				Edges.Add(FIntPoint(FMath::RoundToInt(FMath::Min(A.Y, B.Y)), FMath::RoundToInt(FMath::Max(A.Y, B.Y))));
			}
		}
	}
	return Edges;
}

TSet<FVector> FVoxelLandChaosMeshFixture::GetSeamVertices(
	const FVoxelLandChaosMesh& Mesh, const FVector& Origin, double GlobalX)
{
	TSet<FVector> Vertices;
	for (const FVector3f& Position : Mesh.Vertices)
	{
		const FVector Global = Origin + FVector(Position);
		if (Global.X == GlobalX)
		{
			Vertices.Add(Global);
		}
	}
	return Vertices;
}

bool FVoxelLandChaosMeshFixture::CheckReductions(FAutomationTestBase& Test)
{
	const FVoxelLandChunkData Left = MakeSlope(FIntVector(-1, -1, 0));
	const FVoxelLandChunkData Right = MakeSlope(FIntVector(0, -1, 0));
	const FVector LeftOrigin = VoxelLandMeshCoordinates::RenderOrigin(Left.Key.Coordinate, VoxelSizeCm);
	const FVector RightOrigin = VoxelLandMeshCoordinates::RenderOrigin(Right.Key.Coordinate, VoxelSizeCm);
	FVoxelLandChaosMesh Standard;
	FVoxelLandChaosMesh Neighbor;
	if (!Test.TestTrue(TEXT("Canonical collision builds"),
			FVoxelLandChaosMeshing::Build(Left, VoxelSizeCm, 0, Standard, NeverCancel) == EVoxelLandMeshResult::Success)
		|| !Test.TestTrue(TEXT("Adjacent collision builds"),
			FVoxelLandChaosMeshing::Build(Right, VoxelSizeCm, 0, Neighbor, NeverCancel)
				== EVoxelLandMeshResult::Success))
	{
		return false;
	}

	const double GlobalX = -VoxelSizeCm * 0.5;
	const TSet<FIntPoint> NeighborEdges = GetSeamEdges(Neighbor, RightOrigin, GlobalX);
	const TSet<FVector> NeighborVertices = GetSeamVertices(Neighbor, RightOrigin, GlobalX);
	double StandardArea = 0.0;
	if (!Test.TestTrue(TEXT("Nonempty shared edge"), NeighborEdges.Num() > 0)
		|| !Test.TestTrue(
			TEXT("Canonical mesh has upward nondegenerate triangles"), GetProjectedArea(Standard, StandardArea)))
	{
		return false;
	}
	int32 PreviousTriangles = Standard.Indices.Num() / 3;
	for (uint8 Requested = 1; Requested <= FVoxelLandChaosMeshing::MaxLod; ++Requested)
	{
		FVoxelLandChaosMesh Reduced;
		FVoxelLandChaosMesh Repeat;
		const EVoxelLandMeshResult Result = Async(EAsyncExecution::ThreadPool, [&Left, &Reduced, Requested]() {
			return FVoxelLandChaosMeshing::Build(Left, VoxelSizeCm, Requested, Reduced, NeverCancel);
		}).Get();
		if (!Test.TestTrue(TEXT("LOD reduction builds on a worker"), Result == EVoxelLandMeshResult::Success)
			|| !Test.TestTrue(TEXT("LOD reduction is repeatable"),
				FVoxelLandChaosMeshing::Build(Left, VoxelSizeCm, Requested, Repeat, NeverCancel)
					== EVoxelLandMeshResult::Success))
		{
			return false;
		}

		Test.TestEqual(TEXT("Slope admitted at requested effective LOD"), Reduced.EffectiveLod, Requested);
		const int32 Triangles = Reduced.Indices.Num() / 3;
		Test.TestTrue(TEXT("Higher detail index reduces the triangle count"), Triangles < PreviousTriangles);
		PreviousTriangles = Triangles;
		Test.TestTrue(TEXT("Indices and positions are deterministic"),
			Reduced.Indices == Repeat.Indices && Reduced.Vertices == Repeat.Vertices);

		double ReducedArea = 0.0;
		Test.TestTrue(TEXT("Reduction preserves winding"), GetProjectedArea(Reduced, ReducedArea));
		Test.TestTrue(TEXT("Reduction neither opens holes nor overlaps the slope"),
			FMath::IsNearlyEqual(ReducedArea, StandardArea, 1.e-3));

		const TSet<FVector> ReducedVertices = GetSeamVertices(Reduced, LeftOrigin, GlobalX);
		Test.TestEqual(
			TEXT("Mixed-LOD seam has the canonical vertex count"), ReducedVertices.Num(), NeighborVertices.Num());
		for (const FVector& Vertex : ReducedVertices)
		{
			if (!Test.TestTrue(
					TEXT("Mixed-LOD seam vertices preserve exact XYZ positions"), NeighborVertices.Contains(Vertex)))
			{
				return false;
			}
		}
		const TSet<FIntPoint> ReducedEdges = GetSeamEdges(Reduced, LeftOrigin, GlobalX);
		Test.TestEqual(TEXT("Mixed-LOD seam has the canonical edge count"), ReducedEdges.Num(), NeighborEdges.Num());
		for (const FIntPoint& Edge : ReducedEdges)
		{
			if (!Test.TestTrue(
					TEXT("Each reduced perimeter edge matches its LOD 0 neighbor"), NeighborEdges.Contains(Edge)))
			{
				return false;
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandChaosMeshReductionTest, "VoxelLand.Collision.Chaos.Mesh.ReductionAndSeams",
	FVoxelLandChaosMeshFixture::Flags)

bool FVoxelLandChaosMeshReductionTest::RunTest(const FString& Parameters)
{
	return FVoxelLandChaosMeshFixture::CheckReductions(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandChaosMeshInputTest, "VoxelLand.Collision.Chaos.Mesh.InputAndCancellation",
	FVoxelLandChaosMeshFixture::Flags)

bool FVoxelLandChaosMeshInputTest::RunTest(const FString& Parameters)
{
	using FChaosMeshFixture = FVoxelLandChaosMeshFixture;
	FVoxelLandChaosMesh Result;
	Result.EffectiveLod = 1;
	FVoxelLandChunkData Chunk = FChaosMeshFixture::MakeSlope(FIntVector::ZeroValue);
	TestTrue(TEXT("Cancellation is transactional"),
		FVoxelLandChaosMeshing::Build(Chunk, FChaosMeshFixture::VoxelSizeCm, 2, Result, FChaosMeshFixture::AlwaysCancel)
				== EVoxelLandMeshResult::Cancelled
			&& Result.EffectiveLod == 1);

	Chunk.Key.LOD = 1;
	TestTrue(TEXT("Only completed LOD 0 data can supply collision"),
		FVoxelLandChaosMeshing::Build(Chunk, FChaosMeshFixture::VoxelSizeCm, 0, Result, FChaosMeshFixture::NeverCancel)
				== EVoxelLandMeshResult::InvalidInput
			&& Result.EffectiveLod == 1);
	Chunk.Key.LOD = 0;
	Chunk.Density.Init(1024, FVoxelLandChunkData::SampleCount);
	TestTrue(TEXT("Proven empty data does not produce a collision body payload"),
		FVoxelLandChaosMeshing::Build(Chunk, FChaosMeshFixture::VoxelSizeCm, 0, Result, FChaosMeshFixture::NeverCancel)
				== EVoxelLandMeshResult::Success
			&& Result.Vertices.IsEmpty() && Result.Indices.IsEmpty());

	Chunk.Density.Init(-1024, FVoxelLandChunkData::SampleCount);
	TestTrue(TEXT("Proven solid data has no surface to cook"),
		FVoxelLandChaosMeshing::Build(Chunk, FChaosMeshFixture::VoxelSizeCm, 0, Result, FChaosMeshFixture::NeverCancel)
				== EVoxelLandMeshResult::Success
			&& Result.Vertices.IsEmpty() && Result.Indices.IsEmpty());

	constexpr int32 Axis = FVoxelLandChunkData::SamplesPerAxis;
	constexpr int32 Center = 1 + FVoxelLandChunkData::Cells / 2;
	Chunk.Density.Init(1024, FVoxelLandChunkData::SampleCount);
	Chunk.Density[Center * (1 + Axis + Axis * Axis)] = -1024;
	FVoxelLandChaosMesh SharpFeature;
	TestTrue(TEXT("A sharp isolated feature generates a canonical surface"),
		FVoxelLandChaosMeshing::Build(
			Chunk, FChaosMeshFixture::VoxelSizeCm, 0, SharpFeature, FChaosMeshFixture::NeverCancel)
				== EVoxelLandMeshResult::Success
			&& !SharpFeature.Indices.IsEmpty());
	TestTrue(TEXT("Coarse request preserves a feature that cannot be safely simplified"),
		FVoxelLandChaosMeshing::Build(Chunk, FChaosMeshFixture::VoxelSizeCm, 2, Result, FChaosMeshFixture::NeverCancel)
				== EVoxelLandMeshResult::Success
			&& Result.EffectiveLod == 0 && Result.Vertices == SharpFeature.Vertices
			&& Result.Indices == SharpFeature.Indices);

	Chunk.Density.Pop();
	TestTrue(TEXT("Missing field data is not interpreted as proven empty"),
		FVoxelLandChaosMeshing::Build(Chunk, FChaosMeshFixture::VoxelSizeCm, 0, Result, FChaosMeshFixture::NeverCancel)
			== EVoxelLandMeshResult::InvalidInput);
	return true;
}

#endif
