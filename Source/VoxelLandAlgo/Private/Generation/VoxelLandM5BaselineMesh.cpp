#include "Generation/VoxelLandM5BaselineMesh.h"
#include "Data/Structs/VoxelLandChunkData.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Data/Structs/VoxelLandMeshData.h"
#include "Data/Structs/VoxelLandMeshingScratch.h"
#include "Data/Enums/VoxelLandMeshResult.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM5BoundBaseline.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5ChunkSet.h"
#include "Generation/VoxelLandM5FixedMesh.h"
#include "Generation/VoxelLandM5InfluenceSet.h"
#include "Generation/VoxelLandM5MeshAdapter.h"
#include "Generation/VoxelLandM5MeshExactSource.h"
#include "Generation/VoxelLandM5ValidatedDefinition.h"
#include "Generation/VoxelLandMeshing.h"

class FVoxelLandM5BaselineMeshInternals final
{
public:
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result; Result.Code = Code; Result.Stage = TEXT("M5.BaselineMesh"); Result.Message = Message; return Result;
	}
};
FVoxelLandM5BaselineMesh::FVoxelLandM5BaselineMesh(TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> InBaseline,
	TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> InInfluence, FVoxelLandM5BudgetRef InSourceBudget,
	FVoxelLandM5BudgetRef InJobBudget, TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
	: Baseline(MoveTemp(InBaseline)), Influence(MoveTemp(InInfluence)), SourceBudget(MoveTemp(InSourceBudget)),
	  JobBudget(MoveTemp(InJobBudget)), Lease(MoveTemp(InLease)) {}
FVoxelLandM5BaselineMesh::~FVoxelLandM5BaselineMesh() = default;
bool FVoxelLandM5BaselineMesh::Matches(const FVoxelLandM5BoundBaseline& InBaseline,
	const FVoxelLandM5InfluenceSet& InInfluence, const FVoxelLandM5Budget& InSourceBudget,
	const FVoxelLandM5Budget& InJobBudget) const
{
	return &Baseline.Get() == &InBaseline && &Influence.Get() == &InInfluence
		&& &SourceBudget.Get() == &InSourceBudget && &JobBudget.Get() == &InJobBudget
		&& Lease && InJobBudget.OwnsLease(*Lease);
}
FVoxelLandM5Diagnostic FVoxelLandM5BaselineMesh::Build(TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> InBaseline,
	TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> InInfluence, FVoxelLandM5BudgetRef InSourceBudget,
	FVoxelLandWorkerContext& Worker, FIntVector MinChunk, FIntVector MaxChunkExclusive,
	int32 MaxChunks, int32 MaxTriangles, FVoxelLandM5BudgetRef Budget, TFunctionRef<bool()> IsCancelled,
	TVoxelLandM5ConstPtr<FVoxelLandM5BaselineMesh>& Out)
{
	using I = FVoxelLandM5BaselineMeshInternals;
	using E = EVoxelLandM5ErrorCode;
	using namespace VoxelLand::M5::CheckedMath;
	if (IsCancelled()) return I::Error(E::Cancelled, TEXT("Baseline mesh cancelled"));
	if (IsInGameThread() || &InBaseline->GetDefinition() != &InInfluence->GetDefinition() || !InBaseline->UsesBudget(*InSourceBudget)
		|| !InInfluence->UsesBudget(*InSourceBudget) || &InSourceBudget.Get() == &Budget.Get()
		|| !InBaseline->GetGenerator().OwnsWorkerOnCurrentThread(Worker)
		|| MaxChunks < 1 || MaxChunks > 16 || MaxTriangles < 1 || MaxTriangles > 196608)
		return I::Error(E::InvalidInput, TEXT("Baseline mesh owner, worker or limits invalid"));
	const auto& Snapshot = InBaseline->GetDefinition().GetSnapshot();
	int64 JobBytes = 0, JobWork = 0; Budget->GetLimits(JobBytes, JobWork);
	if (JobBytes > Snapshot.Caves.Limits.MaxBytes || JobWork > Snapshot.Caves.Limits.MaxValidationWork)
		return I::Error(E::InvalidInput, TEXT("Baseline mesh job budget exceeds source limits"));
	const int32 Spacing = Snapshot.Base.VoxelSizeCm;
	if (Spacing < 1 || Spacing > 65536) return I::Error(E::InvalidInput, TEXT("Invalid baseline mesh spacing"));
	int64 ChunkCount = 1;
	const auto& Coverage = InInfluence->GetCoverage();
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int64 Extent = int64(MaxChunkExclusive[Axis]) - int64(MinChunk[Axis]);
		if (Extent < 1 || MinChunk[Axis] < Coverage.MinChunk[Axis]
			|| MaxChunkExclusive[Axis] > Coverage.MaxChunkExclusive[Axis]
			|| !TryMultiplyNonNegative(ChunkCount, Extent, ChunkCount) || ChunkCount > MaxChunks)
			return I::Error(E::OutOfDomain, TEXT("Unsupported baseline owner rectangle"));
	}
	int64 Bytes = 0, Units = 0;
	if (!TryMultiplyNonNegative(ChunkCount, int64(sizeof(FVoxelLandChunkData)) + 2 * int64(FVoxelLandChunkData::SampleCount) * sizeof(int16) + 512, Bytes)
		|| !TryAdd(Bytes, 4096, Bytes) || !TryMultiplyNonNegative(ChunkCount, FVoxelLandChunkData::SampleCount, Units))
		return I::Error(E::ArithmeticOverflow, TEXT("Baseline chunk admission overflow"));
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> ChunkLease;
	if (!Budget->TryChargeWork(Units) || !Budget->TryReserveBytes(Bytes, ChunkLease))
		return I::Error(E::BudgetExceeded, TEXT("Baseline chunk admission exhausted"));
	TArray<FVoxelLandChunkData> Chunks; Chunks.Reserve(int32(ChunkCount));
	for (int64 Z = MinChunk.Z; Z < MaxChunkExclusive.Z; ++Z)
		for (int64 Y = MinChunk.Y; Y < MaxChunkExclusive.Y; ++Y)
			for (int64 X = MinChunk.X; X < MaxChunkExclusive.X; ++X)
			{
				if (IsCancelled()) return I::Error(E::Cancelled, TEXT("Baseline mesh cancelled"));
				FVoxelLandChunkKey Key; Key.Coordinate = FIntVector(int32(X), int32(Y), int32(Z)); Key.LOD = 0;
				if (!InInfluence->CoversChunk(Key)) return I::Error(E::OutOfDomain, TEXT("Baseline owner outside influence coverage"));
				FVoxelLandM5PointCm Min; Min.X = X * 32 - 1; Min.Y = Y * 32 - 1; Min.Z = Z * 32 - 1;
				FVoxelLandChunkData& Chunk = Chunks.AddDefaulted_GetRef(); Chunk.Key = Key;
				const auto Generated = InBaseline->GetGenerator().GenerateFiniteM4Box(Min, FIntVector(35, 35, 35),
					Worker, Budget, IsCancelled, Chunk.Density);
				if (Generated.Code != E::None) return Generated;
				if (Chunk.Density.Num() != FVoxelLandChunkData::SampleCount)
					return I::Error(E::Unproven, TEXT("Incomplete baseline chunk"));
			}
	FVoxelLandM5Diagnostic Result = VoxelLand::M5::ChunkSet::CheckSampleAgreement(
		Chunks, MinChunk, MaxChunkExclusive, MaxChunks, Budget, IsCancelled);
	if (Result.Code != E::None) return Result;
	constexpr int64 MaxVertices = 34 * 34 * 34;
	constexpr int64 OwnedEdges = 32 * 32 * 32 * 3;
	constexpr int64 MeshTriangles = OwnedEdges * 2;
	int64 ScanWork = 0;
	// Count all mesher cells, including the -1..32 halo. A mixed cell can
	// generate a vertex even when no owned edge emits a face. Admit this
	// inventory before reading it; each cell scans eight corners plus setup.
	if (!TryMultiplyNonNegative(Chunks.Num(), OwnedEdges + 9 * MaxVertices + 64, ScanWork))
	{
		return I::Error(E::ArithmeticOverflow, TEXT("Scratch mesh preflight work overflow"));
	}
	if (!Budget->TryChargeWork(ScanWork))
	{
		return I::Error(E::BudgetExceeded, TEXT("Scratch mesh preflight work exhausted"));
	}
	int64 TotalTriangles = 0, PeakTriangles = 0, MixedCells = 0;
	for (const FVoxelLandChunkData& Chunk : Chunks)
	{
		for (int32 Z = -1; Z <= 32; ++Z)
		{
			if (IsCancelled())
			{
				return I::Error(E::Cancelled, TEXT("Scratch mesh preflight cancelled"));
			}
			for (int32 Y = -1; Y <= 32; ++Y)
			{
				for (int32 X = -1; X <= 32; ++X)
				{
					const int32 Index = (X + 1) + 35 * ((Y + 1) + 35 * (Z + 1));
					bool bSolid = false, bAir = false;
					for (int32 DZ = 0; DZ != 2; ++DZ)
					{
						for (int32 DY = 0; DY != 2; ++DY)
						{
							for (int32 DX = 0; DX != 2; ++DX)
							{
								const bool bCornerSolid = Chunk.Density[Index + DX + 35 * DY + 35 * 35 * DZ] < 0;
								bSolid |= bCornerSolid;
								bAir |= !bCornerSolid;
							}
						}
					}
					MixedCells += bSolid && bAir;
				}
			}
		}
		int64 Edges = 0;
		for (int32 Z = 0; Z < 32; ++Z)
		{
			if (IsCancelled())
			{
				return I::Error(E::Cancelled, TEXT("Scratch mesh preflight cancelled"));
			}
			for (int32 Y = 0; Y < 32; ++Y)
			{
				for (int32 X = 0; X < 32; ++X)
				{
					const int32 Index = (X + 1) + 35 * ((Y + 1) + 35 * (Z + 1));
					const bool bSolid = Chunk.Density[Index] < 0;
					Edges += bSolid != (Chunk.Density[Index + 1] < 0);
					Edges += bSolid != (Chunk.Density[Index + 35] < 0);
					Edges += bSolid != (Chunk.Density[Index + 35 * 35] < 0);
				}
			}
		}
		int64 ChunkTriangles = 0;
		if (!TryMultiplyNonNegative(Edges, 2, ChunkTriangles)
			|| !TryAdd(TotalTriangles, ChunkTriangles, TotalTriangles))
		{
			return I::Error(E::ArithmeticOverflow, TEXT("Scratch mesh triangle count overflow"));
		}
		PeakTriangles = FMath::Max(PeakTriangles, ChunkTriangles);
		if (TotalTriangles > MaxTriangles)
		{
			return I::Error(E::BudgetExceeded, TEXT("Scratch mesh triangle limit exhausted"));
		}
	}
	// The mesher allocates vertices and cell indices independently of the
	// edge count. Triangle-dependent indices and adapter arrays are sequential
	// per chunk; fourfold slack covers TArray growth and simultaneous copies.
	// Logical work groups, charged before meshing and adaptation: cell
	// initialization/eight samples (9 per cell); up to 12 edge tests, 12
	// crossing constructions and bounded gradient, normal and snap work
	// (172 per mixed cell); mesher and adapter owned-edge scans (2 per
	// edge); face construction, adapter enclosures/nondegeneracy and exact
	// source checks (112 per predicted triangle); setup and insertion sort.
	// FixedMesh::Convert separately charges 32 per emitted triangle.
	int64 ScratchBytes = 0, TriangleBytes = 0, OutputBytes = 0, Work = 0, SortWork = 0;
	int64 MixedWork = 0, FaceWork = 0;
	if (!TryMultiplyNonNegative(PeakTriangles,
			4 * int64(3 * sizeof(uint32) + 2 * sizeof(FVoxelLandM5ValidationTriangle) + sizeof(uint8)), TriangleBytes)
		|| !TryAdd(4 * MaxVertices * int64(sizeof(FVoxelLandMeshVertex) + sizeof(int32)), 1048576, ScratchBytes)
		|| !TryAdd(ScratchBytes, TriangleBytes, ScratchBytes)
		|| !TryMultiplyNonNegative(TotalTriangles,
			4 * int64(sizeof(FVoxelLandM5FixedTriangleQ18) + sizeof(FVoxelLandM5ChunkFaceSelection)), OutputBytes)
		|| !TryAdd(OutputBytes, 4096, OutputBytes)
		|| !TryMultiplyNonNegative(Chunks.Num(), 9 * MaxVertices + 2 * OwnedEdges + 4096, Work)
		|| !TryMultiplyNonNegative(MixedCells, 172, MixedWork) || !TryAdd(Work, MixedWork, Work)
		|| !TryMultiplyNonNegative(TotalTriangles, 112, FaceWork) || !TryAdd(Work, FaceWork, Work)
		|| !TryMultiplyNonNegative(Chunks.Num(), Chunks.Num(), SortWork) || !TryAdd(Work, SortWork, Work))
	{
		return I::Error(E::ArithmeticOverflow, TEXT("Scratch mesh admission overflow"));
	}
	if (!Budget->TryChargeWork(Work))
	{
		return I::Error(E::BudgetExceeded, TEXT("Scratch mesh work exhausted"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> OutputLease, ScratchLease;
	if (!Budget->TryReserveBytes(OutputBytes, OutputLease) || !Budget->TryReserveBytes(ScratchBytes, ScratchLease))
	{
		return I::Error(E::BudgetExceeded, TEXT("Scratch mesh memory exhausted"));
	}
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Scratch mesh cancelled"));
	}
	TSharedPtr<FVoxelLandM5BaselineMesh, ESPMode::ThreadSafe> Built =
		MakeShareable(new FVoxelLandM5BaselineMesh(InBaseline, InInfluence, InSourceBudget, Budget, MoveTemp(OutputLease)));
	Built->MinInclusive = MinChunk; Built->MaxExclusive = MaxChunkExclusive;
	Built->Triangles.Reserve(int32(TotalTriangles));
	Built->Faces.Reserve(int32(TotalTriangles));
	for (const FVoxelLandChunkData& Chunk : Chunks)
	{
		if (IsCancelled())
		{
			return I::Error(E::Cancelled, TEXT("Scratch mesh cancelled"));
		}
		FVoxelLandMeshingScratch Scratch;
		FVoxelLandMeshData Mesh;
		const EVoxelLandMeshResult Meshed = VoxelLandMeshing::BuildSurfaceNets(
			Chunk, float(Spacing), Scratch, Mesh, IsCancelled, false);
		if (Meshed != EVoxelLandMeshResult::Success)
		{
			return I::Error(Meshed == EVoxelLandMeshResult::Cancelled ? E::Cancelled : E::Unproven,
				TEXT("Scratch CPU meshing failed"));
		}
		TArray<FVoxelLandM5ValidationTriangle> Adapted;
		Result = VoxelLand::M5::MeshAdapter::Adapt(Chunk, float(Spacing), false, Mesh, MaxTriangles,
			32 * 32 * 32 * 3 + MeshTriangles, IsCancelled, Adapted);
		if (Result.Code != E::None)
		{
			return Result;
		}
		if (Adapted.Num() > MaxTriangles - Built->Triangles.Num())
		{
			return I::Error(E::BudgetExceeded, TEXT("Scratch mesh triangle limit exhausted"));
		}
		const FVector Origin = VoxelLandMeshCoordinates::RenderOrigin(Chunk.Key.Coordinate, float(Spacing));
		for (const FVoxelLandM5ValidationTriangle& Triangle : Adapted)
		{
			if (IsCancelled())
			{
				return I::Error(E::Cancelled, TEXT("Scratch mesh cancelled"));
			}
			FVoxelLandM5FixedTriangleQ18 Fixed;
			Result = VoxelLand::M5::FixedMesh::Convert(Triangle, Spacing, true, FVector3d::ZeroVector,
				Budget, IsCancelled, Fixed);
			if (Result.Code != E::None)
			{
				return Result;
			}
			if (!VoxelLand::M5::MeshExactSource::Check(Triangle, Fixed, Mesh, Origin))
			{
				return I::Error(E::Unproven, TEXT("Mesh local-to-origin sum is not exact Q18"));
			}
			FVoxelLandM5ChunkFaceSelection Face;
			Face.ChunkCoordinate = Chunk.Key.Coordinate;
			Face.FaceIndex = Triangle.SourceTriangle;
			Built->Triangles.Add(Fixed);
			Built->Faces.Add(Face);
		}
	}
	if (IsCancelled()) return I::Error(E::Cancelled, TEXT("Baseline mesh cancelled"));
	Out = MoveTemp(Built); return {};
}
