#if WITH_DEV_AUTOMATION_TESTS
#include "VoxelLandM7CheckpointTestFixture.h"
#include "VoxelLandM7PackedFrameTest.h"

#include "Async/Async.h"
#include "Generation/VoxelLandMeshing.h"
#include "Generation/VoxelLandM7NormalFrames.h"
#include "Misc/AutomationTest.h"

class FVoxelLandM7MeshSeamFixture final
{
public:
	static FString Run();

private:
	using FFixture = FVoxelLandM7CheckpointTestFixture;
	using FChunkPtr = FFixture::FChunkPtr;
	static constexpr int32 ChunkCount = 8;
	static constexpr int32 CellAxis = FFixture::Samples - 1;

	static FVoxelLandChunkKey Key(int32 Index, uint8 Lod);
	static int32 OwnedCrossings(const FVoxelLandChunkData& Chunk);
	static bool CompareShared(const FVoxelLandChunkData& A, const FVoxelLandChunkData& B,
		const FVoxelLandMeshData& MeshA, const FVoxelLandMeshData& MeshB,
		const FVoxelLandMeshingScratch& ScratchA, const FVoxelLandMeshingScratch& ScratchB, int32& Vertices);
	static bool CheckLevel(const FVoxelLandGenerator& Generator, uint8 Lod);
	static FVector ExpectedOrigin(const FVoxelLandChunkKey& Key)
	{
		const int32 Spacing = Key.LOD == 0 ? FFixture::VoxelSizeCm : FFixture::LevelSpacingCm;
		return FVector(Key.Coordinate) * double(FFixture::Cells * Spacing);
	}
};

FVoxelLandChunkKey FVoxelLandM7MeshSeamFixture::Key(int32 Index, uint8 Lod)
{
	return {FIntVector((Index & 1) - 1, ((Index >> 1) & 1) - 1, ((Index >> 2) & 1) - 1), Lod};
}

int32 FVoxelLandM7MeshSeamFixture::OwnedCrossings(const FVoxelLandChunkData& Chunk)
{
	const FIntVector Edges[] = {FIntVector(1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, 0, 1)};
	int32 CrossingCount = 0;
	for (int32 Z = 0; Z < FFixture::Cells; ++Z)
	{
		for (int32 Y = 0; Y < FFixture::Cells; ++Y)
		{
			for (int32 X = 0; X < FFixture::Cells; ++X)
			{
				const FIntVector Cell(X, Y, Z);
				const bool bSolid = Chunk.Density[FFixture::Index(Cell)] < 0;
				for (const FIntVector& Step : Edges)
				{
					const bool bOtherSolid = Chunk.Density[FFixture::Index(Cell + Step)] < 0;
					CrossingCount += int32(bSolid != bOtherSolid);
				}
			}
		}
	}
	return CrossingCount;
}

bool FVoxelLandM7MeshSeamFixture::CompareShared(const FVoxelLandChunkData& A, const FVoxelLandChunkData& B,
	const FVoxelLandMeshData& MeshA, const FVoxelLandMeshData& MeshB, const FVoxelLandMeshingScratch& ScratchA,
	const FVoxelLandMeshingScratch& ScratchB, int32& Vertices)
{
	const FIntVector BaseA = A.Key.Coordinate * FFixture::Cells;
	const FIntVector BaseB = B.Key.Coordinate * FFixture::Cells;
	const FIntVector Min = BaseA.ComponentMax(BaseB) - FIntVector(1);
	const FIntVector Max = BaseA.ComponentMin(BaseB) + FIntVector(FFixture::Cells + 1);
	const FVector OriginA = ExpectedOrigin(A.Key);
	const FVector OriginB = ExpectedOrigin(B.Key);
	Vertices = 0;
	for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
	{
		for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
		{
			for (int32 X = Min.X; X <= Max.X; ++X)
			{
				const FIntVector Global(X, Y, Z);
				const FIntVector LA = Global - BaseA;
				const FIntVector LB = Global - BaseB;
				if (A.Density[FFixture::Index(LA)] != B.Density[FFixture::Index(LB)])
				{
					return false;
				}
				if (X == Max.X || Y == Max.Y || Z == Max.Z)
				{
					continue;
				}
				const int32 VA = ScratchA.CellVertices[FFixture::Index(LA, CellAxis)];
				const int32 VB = ScratchB.CellVertices[FFixture::Index(LB, CellAxis)];
				if ((VA == INDEX_NONE) != (VB == INDEX_NONE))
				{
					return false;
				}
				if (VA != INDEX_NONE)
				{
					const FVoxelLandMeshVertex& First = MeshA.Vertices[VA];
					const FVoxelLandMeshVertex& Second = MeshB.Vertices[VB];
					const FVector LogicalA = OriginA + FVector(First.Position);
					const FVector LogicalB = OriginB + FVector(Second.Position);
					const FVector Rebase(-51200.0, 38400.0, -6400.0);
					const FVector RebasedB = (OriginB - Rebase) + FVector(Second.Position);
					FVoxelLandM7NormalAnchor AAnchor, BAnchor;
					if (LogicalA != LogicalB
						|| !FVoxelLandM7NormalFrames::MakeAnchor(LogicalA, FFixture::VoxelSizeCm, AAnchor)
						|| !FVoxelLandM7NormalFrames::MakeAnchor(RebasedB + Rebase, FFixture::VoxelSizeCm, BAnchor)
						|| !(AAnchor == BAnchor)
						|| !FVoxelLandM7PackedFrameTest::Matches(First.Normal, First.Tangent, Second.Normal, Second.Tangent))
					{
						return false;
					}
					++Vertices;
				}
			}
		}
	}
	return true;
}

bool FVoxelLandM7MeshSeamFixture::CheckLevel(const FVoxelLandGenerator& Generator, uint8 Lod)
{
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	if (Generator.CreateWorker(Worker))
	{
		return false;
	}
	FChunkPtr Chunks[ChunkCount];
	FVoxelLandMeshData Meshes[ChunkCount];
	FVoxelLandMeshingScratch Scratch[ChunkCount];
	for (int32 Index = 0; Index < ChunkCount; ++Index)
	{
		if (Generator.GenerateChunk(Key(Index, Lod), *Worker, Chunks[Index]) || !Chunks[Index]
			|| VoxelLandMeshing::BuildSurfaceNets(*Chunks[Index], FFixture::VoxelSizeCm, Scratch[Index], Meshes[Index],
				&FFixture::NeverCancel) != EVoxelLandMeshResult::Success
			|| Meshes[Index].Indices.Num() != 6 * OwnedCrossings(*Chunks[Index]))
		{
			return false;
		}
		const FVector Origin = ExpectedOrigin(Chunks[Index]->Key);
		for (const FVoxelLandMeshVertex& Vertex : Meshes[Index].Vertices)
		{
			const FVector Point = Origin + FVector(Vertex.Position);
			if (FMath::Abs(Point.Z - FFixture::HeightCm(Point)) > 0.5)
			{
				return false;
			}
		}
	}

	int32 VerticesByAdjacency[ChunkCount] = {};
	for (int32 A = 0; A < ChunkCount; ++A)
	{
		for (int32 B = A + 1; B < ChunkCount; ++B)
		{
			int32 Vertices = 0;
			if (!CompareShared(*Chunks[A], *Chunks[B], Meshes[A], Meshes[B], Scratch[A], Scratch[B], Vertices))
			{
				return false;
			}
			VerticesByAdjacency[A ^ B] += Vertices;
		}
	}
	// Each nonzero bit mask identifies a face, edge or corner orientation.
	for (int32 Mask = 1; Mask < ChunkCount; ++Mask)
	{
		if (VerticesByAdjacency[Mask] == 0)
		{
			return false;
		}
	}

	for (int32 Index = ChunkCount - 1; Index >= 0; --Index)
	{
		FChunkPtr Reverse;
		FVoxelLandMeshData Mesh;
		FVoxelLandMeshingScratch ReverseScratch;
		if (Generator.GenerateChunk(Key(Index, Lod), *Worker, Reverse) || !Reverse
			|| Reverse->Hash != Chunks[Index]->Hash || Reverse->Density != Chunks[Index]->Density
			|| VoxelLandMeshing::BuildSurfaceNets(*Reverse, FFixture::VoxelSizeCm, ReverseScratch, Mesh,
				&FFixture::NeverCancel) != EVoxelLandMeshResult::Success
			|| Mesh.Indices != Meshes[Index].Indices || Mesh.Vertices.Num() != Meshes[Index].Vertices.Num())
		{
			return false;
		}
		for (int32 Vertex = 0; Vertex < Mesh.Vertices.Num(); ++Vertex)
		{
			const FVoxelLandMeshVertex& First = Meshes[Index].Vertices[Vertex];
			const FVoxelLandMeshVertex& Second = Mesh.Vertices[Vertex];
			if (First.Position != Second.Position
				|| !FVoxelLandM7PackedFrameTest::Matches(First.Normal, First.Tangent, Second.Normal, Second.Tangent))
			{
				return false;
			}
		}
	}
	return true;
}

FString FVoxelLandM7MeshSeamFixture::Run()
{
	FVoxelLandGeneratorSettings Settings;
	FFixture::FGridPtr Grid;
	FFixture::FGeneratorPtr Generator;
	if (!FFixture::CreateSettings(Settings) || !FFixture::CreateTiltedGrid(-16000, 32, 1000, Grid)
		|| !FFixture::CreateGenerator(Settings, Grid, false, false, Generator))
	{
		return TEXT("Finite tilted surface setup failed");
	}
	if (!CheckLevel(*Generator, 0))
	{
		return TEXT("LOD0 finite tilt, ownership, shared positions or request-order regression");
	}
	if (!CheckLevel(*Generator, FFixture::CoarseLod))
	{
		return TEXT("LOD1 finite tilt, ownership, shared positions or request-order mismatch");
	}
	return {};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM7MeshSeamTest, "VoxelLand.M7.Lod.MeshSeams",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM7MeshSeamTest::RunTest(const FString& Parameters)
{
	const FString Error = Async(EAsyncExecution::ThreadPool, &FVoxelLandM7MeshSeamFixture::Run).Get();
	return TestTrue(Error.IsEmpty() ? TEXT("Ordinary two-level finite CPU mesh seams and ownership") : Error, Error.IsEmpty());
}
#endif
