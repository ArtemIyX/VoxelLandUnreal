#include "Generation/Caves/Field/VoxelLandCaveScratchMesh.h"

#include "Data/Meshing/VoxelLandMeshResult.h"
#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Data/Meshing/VoxelLandMeshData.h"
#include "Data/Meshing/VoxelLandMeshingScratch.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"
#include "Generation/Caves/Field/VoxelLandCaveChunkSet.h"
#include "Generation/Caves/Field/VoxelLandCaveFixedMesh.h"
#include "Generation/Caves/Field/VoxelLandCaveMeshAdapter.h"
#include "VoxelLandCaveMeshExactSource.h"
#include "Generation/Caves/Field/VoxelLandCaveScratchChunks.h"
#include "Generation/Meshing/VoxelLandMeshing.h"
#include <cmath>

class FVoxelLandM5ScratchMeshInternals final
{
public:
	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.ScratchMesh");
		Result.Message = Message;
		return Result;
	}
};

bool FVoxelLandM5ScratchMesh::IsOwnedBy(const FVoxelLandM5Budget& Budget) const
{
	return Lease.IsValid() && Budget.OwnsLease(*Lease);
}

FVoxelLandM5Diagnostic FVoxelLandM5ScratchMesh::Build(
	TSharedRef<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Source, int32 MaxChunks, int32 MaxTriangles,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5ScratchMesh, ESPMode::ThreadSafe>& Out)
{
	using I = FVoxelLandM5ScratchMeshInternals;
	using E = EVoxelLandM5ErrorCode;
	using namespace VoxelLand::M5::CheckedMath;
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Scratch mesh cancelled"));
	}
	const int32 Spacing = Source->GetVoxelSizeCm();
	if (IsInGameThread() || !Source->IsOwnedBy(*Budget) || MaxChunks < 1 || MaxChunks > 16 || MaxTriangles < 1
		|| MaxTriangles > 196608 || Spacing < 1 || Spacing > 134217)
	{
		return I::Error(E::InvalidInput, TEXT("Invalid scratch mesh source or limits"));
	}
	const TConstArrayView<FVoxelLandChunkData> Chunks = Source->GetChunks();
	FVoxelLandM5Diagnostic Result = VoxelLand::M5::ChunkSet::CheckSampleAgreement(
		Chunks, Source->GetMinInclusive(), Source->GetMaxExclusive(), MaxChunks, Budget, IsCancelled);
	if (Result.Code != E::None)
	{
		return Result;
	}
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
	TSharedPtr<FVoxelLandM5ScratchMesh, ESPMode::ThreadSafe> Built =
		MakeShareable(new FVoxelLandM5ScratchMesh(MoveTemp(OutputLease), Source));
	Built->Triangles.Reserve(int32(TotalTriangles));
	Built->Faces.Reserve(int32(TotalTriangles));
	// ChunkSet has already rejected duplicates. At most sixteen indices fit
	// the temporary allowance; insertion sort is deterministic and bounded.
	TArray<int32> Order;
	Order.Reserve(Chunks.Num());
	for (int32 Index = 0; Index < Chunks.Num(); ++Index)
	{
		if (IsCancelled())
		{
			return I::Error(E::Cancelled, TEXT("Scratch mesh cancelled"));
		}
		int32 At = Order.Num();
		Order.Add(Index);
		const FIntVector Key = Chunks[Index].Key.Coordinate;
		while (At > 0)
		{
			const FIntVector Previous = Chunks[Order[At - 1]].Key.Coordinate;
			const bool bBefore =
				Key.Z < Previous.Z
				|| (Key.Z == Previous.Z && (Key.Y < Previous.Y || (Key.Y == Previous.Y && Key.X < Previous.X)));
			if (!bBefore)
			{
				break;
			}
			Order[At] = Order[At - 1];
			--At;
		}
		Order[At] = Index;
	}
	for (int32 ChunkIndex : Order)
	{
		const FVoxelLandChunkData& Chunk = Chunks[ChunkIndex];
		if (IsCancelled())
		{
			return I::Error(E::Cancelled, TEXT("Scratch mesh cancelled"));
		}
		FVoxelLandMeshingScratch Scratch;
		FVoxelLandMeshData Mesh;
		const EVoxelLandMeshResult Meshed =
			VoxelLandMeshing::BuildSurfaceNets(Chunk, float(Spacing), Scratch, Mesh, IsCancelled, false);
		if (Meshed != EVoxelLandMeshResult::Success)
		{
			return I::Error(Meshed == EVoxelLandMeshResult::Cancelled ? E::Cancelled : E::Unproven,
				TEXT("Scratch CPU meshing failed"));
		}
		TArray<FVoxelLandM5ValidationTriangle> Adapted;
		Result = VoxelLand::M5::MeshAdapter::Adapt(
			Chunk, float(Spacing), false, Mesh, MaxTriangles, 32 * 32 * 32 * 3 + MeshTriangles, IsCancelled, Adapted);
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
			Result = VoxelLand::M5::FixedMesh::Convert(
				Triangle, Spacing, true, FVector3d::ZeroVector, Budget, IsCancelled, Fixed);
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
	if (IsCancelled())
	{
		return I::Error(E::Cancelled, TEXT("Scratch mesh cancelled"));
	}
	Out = MoveTemp(Built);
	return {};
}
