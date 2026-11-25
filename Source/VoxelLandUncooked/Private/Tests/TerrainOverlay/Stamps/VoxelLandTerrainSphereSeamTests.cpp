#if WITH_DEV_AUTOMATION_TESTS
#include "VoxelLandTerrainSphereTestFixture.h"

#include "Async/Async.h"
#include "Generation/Meshing/VoxelLandMeshing.h"
#include "Misc/AutomationTest.h"
#include <cmath>

class FVoxelLandM6SphereSeamFixture final
{
public:
	static FString Run();

private:
	using FFixture = FVoxelLandM6SphereTestFixture;
	static constexpr int32 Cells = FFixture::Cells;
	static constexpr int32 Samples = FFixture::Samples;
	static constexpr int32 CellAxis = Samples - 1;
	static constexpr int32 ChunkCount = 8;

	static FIntVector ChunkCoordinate(int32 Index);
	static int16 AnalyticSample(const FVoxelLandM6SphereStamp& Stamp, const FIntVector& GlobalLattice);
	static bool MatchesAnalyticSamples(const FVoxelLandChunkData& Chunk, const FVoxelLandM6SphereStamp& Stamp);
	static int32 OwnedCrossings(const FVoxelLandChunkData& Chunk);
	static bool CompareShared(int32 A, int32 B, const FVoxelLandChunkData* const* Chunks,
		const FVoxelLandMeshData* Meshes, const FVoxelLandMeshingScratch* Scratch);
};

FIntVector FVoxelLandM6SphereSeamFixture::ChunkCoordinate(int32 Index)
{
	return FIntVector((Index & 1) - 1, ((Index >> 1) & 1) - 1, ((Index >> 2) & 1) - 1);
}

int16 FVoxelLandM6SphereSeamFixture::AnalyticSample(
	const FVoxelLandM6SphereStamp& Stamp, const FIntVector& GlobalLattice)
{
	const FVector Delta = FVector(GlobalLattice) * FFixture::VoxelSizeCm - FVector(Stamp.CenterCm);
	const double DistanceCm = std::sqrt(Delta.X * Delta.X + Delta.Y * Delta.Y + Delta.Z * Delta.Z);
	const double SignedCm = std::floor(DistanceCm * 256.0) / 256.0 - Stamp.RadiusCm;
	return int16(
		FMath::Clamp(std::round(SignedCm / FFixture::VoxelSizeCm * 1024.0), double(MIN_int16), double(MAX_int16)));
}

bool FVoxelLandM6SphereSeamFixture::MatchesAnalyticSamples(
	const FVoxelLandChunkData& Chunk, const FVoxelLandM6SphereStamp& Stamp)
{
	const FIntVector Base = Chunk.Key.Coordinate * Cells;
	for (int32 Z = -1; Z <= Cells + 1; ++Z)
	{
		for (int32 Y = -1; Y <= Cells + 1; ++Y)
		{
			for (int32 X = -1; X <= Cells + 1; ++X)
			{
				const FIntVector Local(X, Y, Z);
				if (Chunk.Density[FFixture::SampleIndex(Local, Samples)] != AnalyticSample(Stamp, Base + Local))
				{
					return false;
				}
			}
		}
	}
	return true;
}

int32 FVoxelLandM6SphereSeamFixture::OwnedCrossings(const FVoxelLandChunkData& Chunk)
{
	int32 Crossings = 0;
	const FIntVector Edges[] = {FIntVector(1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, 0, 1)};
	for (int32 Z = 0; Z < Cells; ++Z)
	{
		for (int32 Y = 0; Y < Cells; ++Y)
		{
			for (int32 X = 0; X < Cells; ++X)
			{
				const FIntVector Start(X, Y, Z);
				const bool bStartSolid = Chunk.Density[FFixture::SampleIndex(Start, Samples)] < 0;
				for (const FIntVector& Axis : Edges)
				{
					const bool bEndSolid = Chunk.Density[FFixture::SampleIndex(Start + Axis, Samples)] < 0;
					Crossings += int32(bEndSolid != bStartSolid);
				}
			}
		}
	}
	return Crossings;
}

bool FVoxelLandM6SphereSeamFixture::CompareShared(int32 A, int32 B, const FVoxelLandChunkData* const* Chunks,
	const FVoxelLandMeshData* Meshes, const FVoxelLandMeshingScratch* Scratch)
{
	const FIntVector K0 = ChunkCoordinate(A);
	const FIntVector K1 = ChunkCoordinate(B);
	const FIntVector BaseA = K0 * Cells;
	const FIntVector BaseB = K1 * Cells;
	const FIntVector Min = BaseA.ComponentMax(BaseB) - FIntVector(1);
	const FIntVector Max = BaseA.ComponentMin(BaseB) + FIntVector(Cells + 1);
	const FVector OriginA = VoxelLandMeshCoordinates::RenderOrigin(K0, FFixture::VoxelSizeCm);
	const FVector OriginB = VoxelLandMeshCoordinates::RenderOrigin(K1, FFixture::VoxelSizeCm);

	int32 SharedVertices = 0;
	for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
	{
		for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
		{
			for (int32 X = Min.X; X <= Max.X; ++X)
			{
				const FIntVector Global(X, Y, Z);
				const FIntVector LocalA = Global - BaseA;
				const FIntVector LocalB = Global - BaseB;
				if (Chunks[A]->Density[FFixture::SampleIndex(LocalA, Samples)]
					!= Chunks[B]->Density[FFixture::SampleIndex(LocalB, Samples)])
				{
					return false;
				}
				// The uppermost shared sample is the far corner of the last shared cell.
				if (X == Max.X || Y == Max.Y || Z == Max.Z)
				{
					continue;
				}
				const int32 VA = Scratch[A].CellVertices[FFixture::SampleIndex(LocalA, CellAxis)];
				const int32 VB = Scratch[B].CellVertices[FFixture::SampleIndex(LocalB, CellAxis)];
				if ((VA == INDEX_NONE) != (VB == INDEX_NONE))
				{
					return false;
				}
				if (VA != INDEX_NONE)
				{
					if (OriginA + FVector(Meshes[A].Vertices[VA].Position)
						!= OriginB + FVector(Meshes[B].Vertices[VB].Position))
					{
						return false;
					}
					++SharedVertices;
				}
			}
		}
	}
	return SharedVertices > 0;
}

FString FVoxelLandM6SphereSeamFixture::Run()
{
	FVoxelLandGeneratorSettings Settings;
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	// This center/radius crosses all three origin planes and the shared corner cells.
	const FVoxelLandM6SphereStamp Stamp =
		FFixture::Stamp(1, EVoxelLandM6SphereOperation::Add, FIntVector(-434, -411, -390), 765);
	Settings.M6Spheres.Stamps = MakeArrayView(&Stamp, 1);
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	if (!FFixture::CreateInputs(-8000, Settings, Grid) || FFixture::CreateGenerator(Settings, Grid, true, Generator)
		|| Generator->CreateWorker(Worker))
	{
		return TEXT("Could not create the finite M4 seam fixture with compatibility smoothing");
	}

	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Data[ChunkCount];
	const FVoxelLandChunkData* Chunks[ChunkCount] = {};
	FVoxelLandMeshData Meshes[ChunkCount];
	FVoxelLandMeshingScratch Scratch[ChunkCount];
	for (int32 Index = 0; Index < ChunkCount; ++Index)
	{
		const FIntVector Coordinate = ChunkCoordinate(Index);
		if (Generator->GenerateChunk({Coordinate}, *Worker, Data[Index])
			|| !MatchesAnalyticSamples(*Data[Index], Stamp))
		{
			return FString::Printf(
				TEXT("Finite padded sphere density differs from the oracle in %s"), *Coordinate.ToString());
		}
		Chunks[Index] = Data[Index].Get();
		if (VoxelLandMeshing::BuildSurfaceNets(
				*Chunks[Index], FFixture::VoxelSizeCm, Scratch[Index], Meshes[Index], &FFixture::NeverCancel)
				!= EVoxelLandMeshResult::Success
			|| Meshes[Index].Indices.IsEmpty() || Meshes[Index].Indices.Num() != 6 * OwnedCrossings(*Chunks[Index]))
		{
			return FString::Printf(TEXT("CPU mesh face ownership/count differs in %s"), *Coordinate.ToString());
		}
	}

	for (int32 A = 0; A < ChunkCount; ++A)
	{
		for (int32 B = A + 1; B < ChunkCount; ++B)
		{
			if (!CompareShared(A, B, Chunks, Meshes, Scratch))
			{
				return FString::Printf(TEXT("Padded density or exact shared-cell mesh seam differs between %s and %s"),
					*ChunkCoordinate(A).ToString(), *ChunkCoordinate(B).ToString());
			}
		}
	}

	for (int32 Index = ChunkCount - 1; Index >= 0; --Index)
	{
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Reverse;
		const FIntVector Coordinate = ChunkCoordinate(Index);
		if (Generator->GenerateChunk({Coordinate}, *Worker, Reverse) || Chunks[Index]->Hash != Reverse->Hash
			|| Chunks[Index]->Density != Reverse->Density)
		{
			return FString::Printf(TEXT("Reversed request order changed density in %s"), *Coordinate.ToString());
		}
		FVoxelLandMeshData ReverseMesh;
		FVoxelLandMeshingScratch ReverseScratch;
		if (VoxelLandMeshing::BuildSurfaceNets(
				*Reverse, FFixture::VoxelSizeCm, ReverseScratch, ReverseMesh, &FFixture::NeverCancel)
				!= EVoxelLandMeshResult::Success
			|| Meshes[Index].Indices != ReverseMesh.Indices
			|| Meshes[Index].Vertices.Num() != ReverseMesh.Vertices.Num())
		{
			return FString::Printf(TEXT("Reversed request order changed mesh ownership in %s"), *Coordinate.ToString());
		}
		for (int32 Vertex = 0; Vertex < ReverseMesh.Vertices.Num(); ++Vertex)
		{
			if (Meshes[Index].Vertices[Vertex].Position != ReverseMesh.Vertices[Vertex].Position)
			{
				return FString::Printf(
					TEXT("Reversed request order changed a mesh position in %s"), *Coordinate.ToString());
			}
		}
	}
	return {};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM6SphereSeamTest, "VoxelLand.M6.Sphere.RealFiniteSeams",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM6SphereSeamTest::RunTest(const FString& Parameters)
{
	const FString Error = Async(EAsyncExecution::ThreadPool, &FVoxelLandM6SphereSeamFixture::Run).Get();
	return TestTrue(Error.IsEmpty() ? TEXT("Finite sphere density and Surface Nets seams") : Error, Error.IsEmpty());
}
#endif
