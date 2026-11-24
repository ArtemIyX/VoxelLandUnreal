#include "Generation/VoxelLandM5ScratchMesh.h"

#include "Data/Enums/VoxelLandMeshResult.h"
#include "Data/Structs/VoxelLandChunkData.h"
#include "Data/Structs/VoxelLandMeshData.h"
#include "Data/Structs/VoxelLandMeshingScratch.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5ChunkSet.h"
#include "Generation/VoxelLandM5FixedMesh.h"
#include "Generation/VoxelLandM5MeshAdapter.h"
#include "Generation/VoxelLandM5ScratchChunks.h"
#include "Generation/VoxelLandMeshing.h"
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
	static bool ToQ18(double Value, int64& Out)
	{
		constexpr double Limit = double(int64(1) << 39);
		const double Scaled = Value * 262144.0;
		if (!std::isfinite(Scaled) || Scaled < -Limit || Scaled > Limit)
		{
			return false;
		}
		const int64 Fixed = int64(Scaled);
		if (double(Fixed) != Scaled)
		{
			return false;
		}
		Out = Fixed;
		return true;
	}
	// Verify the adapter's local+origin sum against exact Q18 integer addition,
	// rather than relying on the rounded binary64 sum being on the snap grid.
	static bool ExactSource(const FVoxelLandM5ValidationTriangle& Triangle, const FVoxelLandM5FixedTriangleQ18& Fixed,
		const FVoxelLandMeshData& Mesh, const FVector& Origin)
	{
		if (Triangle.SourceTriangle < 0 || int64(Triangle.SourceTriangle) * 3 + 2 >= Mesh.Indices.Num())
		{
			return false;
		}
		const FVoxelLandM5FixedPointQ18* Points[] = {&Fixed.A, &Fixed.B, &Fixed.C};
		for (int32 Vertex = 0; Vertex < 3; ++Vertex)
		{
			const uint32 Index = Mesh.Indices[Triangle.SourceTriangle * 3 + Vertex];
			if (Index >= uint32(Mesh.Vertices.Num()))
			{
				return false;
			}
			const FVector3f& Local = Mesh.Vertices[Index].Position;
			const int64 Values[] = {Points[Vertex]->X, Points[Vertex]->Y, Points[Vertex]->Z};
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				int64 A = 0, B = 0, Sum = 0;
				if (!ToQ18(double(Origin[Axis]), A) || !ToQ18(double(Local[Axis]), B)
					|| !VoxelLand::M5::CheckedMath::TryAdd(A, B, Sum) || Sum != Values[Axis])
				{
					return false;
				}
			}
		}
		return true;
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
	if (IsInGameThread() || !Source->IsOwnedBy(*Budget) || MaxChunks < 1 || MaxChunks > 16
		|| MaxTriangles < 1 || MaxTriangles > 196608 || Spacing < 1 || Spacing > 134217)
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
	constexpr int64 MeshTriangles = 32 * 32 * 32 * 3 * 2;
	const int64 ScratchBytes = 4 * MaxVertices * (sizeof(FVoxelLandMeshVertex) + sizeof(int32))
		+ 4 * MeshTriangles * (3 * sizeof(uint32) + 2 * sizeof(FVoxelLandM5ValidationTriangle) + sizeof(uint8)) + 1048576;
	int64 OutputBytes = 0, Work = 0, SortWork = 0;
	if (!TryMultiplyNonNegative(MaxTriangles,
			4 * int64(sizeof(FVoxelLandM5FixedTriangleQ18) + sizeof(FVoxelLandM5ChunkFaceSelection)), OutputBytes)
		|| !TryAdd(OutputBytes, 4096, OutputBytes) || !TryMultiplyNonNegative(Chunks.Num(), 4000000, Work)
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
	Built->Triangles.Reserve(MaxTriangles);
	Built->Faces.Reserve(MaxTriangles);
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
			const bool bBefore = Key.Z < Previous.Z || (Key.Z == Previous.Z && (Key.Y < Previous.Y
				|| (Key.Y == Previous.Y && Key.X < Previous.X)));
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
			if (!I::ExactSource(Triangle, Fixed, Mesh, Origin))
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
