#include "Generation/Meshing/VoxelLandChaosMeshing.h"

#include "Data/Meshing/VoxelLandChaosMesh.h"
#include "Generation/Meshing/VoxelLandMeshing.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

class FVoxelLandChaosMeshTopology final
{
public:
	explicit FVoxelLandChaosMeshTopology(const FVoxelLandMeshData& Source);

	bool Simplify(TConstArrayView<int32> CellVertices, uint8 RequestedLod, float VoxelSizeCm,
		TFunctionRef<bool()> IsCancelled, uint8& OutEffectiveLod);
	bool Export(FVoxelLandChaosMesh& Staged, TFunctionRef<bool()> IsCancelled) const;
	static bool ExportCanonical(
		const FVoxelLandMeshData& Source, FVoxelLandChaosMesh& Staged, TFunctionRef<bool()> IsCancelled);

private:
	using FIncidentTriangles = TArray<int32, TInlineAllocator<8>>;
	using FRing = TArray<FIntPoint, TInlineAllocator<8>>;

	static constexpr int32 Axes = 3;
	static constexpr int32 TriangleSize = 3;
	static constexpr int32 PollMask = 255;
	static constexpr int32 CellAxis = FVoxelLandChunkData::SamplesPerAxis - 1;
	static constexpr int32 MaxFan = 24;
	static constexpr float MinimumNormalDot = 0.98f;
	static constexpr float MaximumDeviationVoxels = 0.025f;

	static uint64 EdgeKey(int32 A, int32 B);
	static int32 LatticeRank(const FIntVector& Cell, int32 Stride);
	static FIntVector ReplaceVertex(const FIntVector& Triangle, int32 From, int32 To);
	static bool ContainsVertex(const FIntVector& Triangle, int32 Vertex);
	static bool IsClosedRing(const FRing& Ring);
	static void AppendVertex(
		const FVoxelLandMeshData& Source, int32 Old, TArray<int32>& Remap, FVoxelLandChaosMesh& Staged);

	bool PinBoundaries(TFunctionRef<bool()> IsCancelled);
	bool BuildRing(int32 From, FRing& Ring) const;
	bool IsCompatibleGeometry(const FIntVector& Triangle, int32 From, int32 To, float Tolerance) const;
	bool IsCoplanarFan(const FIntVector& Triangle, const FRing& Ring, float Tolerance) const;
	bool IsCompatibleTarget(int32 From, int32 To, const FRing& Ring, float Tolerance) const;
	int32 FindCollapseTarget(int32 From, int32 Stride, float Tolerance) const;
	void Collapse(int32 From, int32 To);
	bool SimplifyPass(uint8 Lod, float VoxelSizeCm, TFunctionRef<bool()> IsCancelled);

	const FVoxelLandMeshData& Original;
	TArray<FIntVector> Triangles;
	TArray<FIncidentTriangles> VertexTriangles;
	TArray<FIntVector> Cells;
	TBitArray<> Pinned;
	int32 ActiveTriangles = 0;
};

FVoxelLandChaosMeshTopology::FVoxelLandChaosMeshTopology(const FVoxelLandMeshData& Source)
	: Original(Source)
{
	Triangles.Reserve(Source.Indices.Num() / TriangleSize);
	VertexTriangles.SetNum(Source.Vertices.Num());
	Pinned.Init(false, Source.Vertices.Num());
	for (int32 Index = 0; Index < Source.Indices.Num(); Index += TriangleSize)
	{
		const FIntVector Triangle(static_cast<int32>(Source.Indices[Index]),
			static_cast<int32>(Source.Indices[Index + 1]), static_cast<int32>(Source.Indices[Index + 2]));
		const int32 TriangleId = Triangles.Add(Triangle);
		for (int32 Axis = 0; Axis < Axes; ++Axis)
		{
			VertexTriangles[Triangle[Axis]].Add(TriangleId);
		}
	}
	ActiveTriangles = Triangles.Num();
}

uint64 FVoxelLandChaosMeshTopology::EdgeKey(int32 A, int32 B)
{
	return (static_cast<uint64>(FMath::Min(A, B)) << 32) | static_cast<uint32>(FMath::Max(A, B));
}

int32 FVoxelLandChaosMeshTopology::LatticeRank(const FIntVector& Cell, int32 Stride)
{
	return int32(Cell.X % Stride != 0) + int32(Cell.Y % Stride != 0) + int32(Cell.Z % Stride != 0);
}

FIntVector FVoxelLandChaosMeshTopology::ReplaceVertex(const FIntVector& Triangle, int32 From, int32 To)
{
	return FIntVector(Triangle.X == From ? To : Triangle.X, Triangle.Y == From ? To : Triangle.Y,
		Triangle.Z == From ? To : Triangle.Z);
}

bool FVoxelLandChaosMeshTopology::ContainsVertex(const FIntVector& Triangle, int32 Vertex)
{
	return Triangle.X == Vertex || Triangle.Y == Vertex || Triangle.Z == Vertex;
}

bool FVoxelLandChaosMeshTopology::PinBoundaries(TFunctionRef<bool()> IsCancelled)
{
	TMap<uint64, int32> EdgeUses;
	EdgeUses.Reserve(Triangles.Num() * TriangleSize);
	for (int32 Index = 0; Index < Triangles.Num(); ++Index)
	{
		if ((Index & PollMask) == 0 && IsCancelled())
		{
			return false;
		}
		const FIntVector& Triangle = Triangles[Index];
		for (int32 Corner = 0; Corner < TriangleSize; ++Corner)
		{
			++EdgeUses.FindOrAdd(EdgeKey(Triangle[Corner], Triangle[(Corner + 1) % TriangleSize]));
		}
	}

	// Preserve whole triangles incident to the shared owner boundary. The neighboring
	// tile can remain at LOD 0 or choose its own interior reduction without creating T-junctions.
	for (int32 Index = 0; Index < Triangles.Num(); ++Index)
	{
		if ((Index & PollMask) == 0 && IsCancelled())
		{
			return false;
		}
		const FIntVector& Triangle = Triangles[Index];
		if (EdgeUses.FindChecked(EdgeKey(Triangle.X, Triangle.Y)) != 2
			|| EdgeUses.FindChecked(EdgeKey(Triangle.Y, Triangle.Z)) != 2
			|| EdgeUses.FindChecked(EdgeKey(Triangle.Z, Triangle.X)) != 2)
		{
			Pinned[Triangle.X] = true;
			Pinned[Triangle.Y] = true;
			Pinned[Triangle.Z] = true;
		}
	}
	return true;
}

bool FVoxelLandChaosMeshTopology::IsClosedRing(const FRing& Ring)
{
	int32 Head = Ring[0].X;
	for (int32 Step = 0; Step < Ring.Num(); ++Step)
	{
		int32 Next = INDEX_NONE;
		for (const FIntPoint& Link : Ring)
		{
			if (Link.X == Head)
			{
				if (Next != INDEX_NONE)
				{
					return false;
				}
				Next = Link.Y;
			}
		}
		if (Next == INDEX_NONE || (Next == Ring[0].X && Step + 1 != Ring.Num()))
		{
			return false;
		}
		Head = Next;
	}
	return Head == Ring[0].X;
}

bool FVoxelLandChaosMeshTopology::BuildRing(int32 From, FRing& Ring) const
{
	const FIncidentTriangles& Incident = VertexTriangles[From];
	if (Incident.Num() < TriangleSize || Incident.Num() > MaxFan)
	{
		return false;
	}
	for (const int32 TriangleId : Incident)
	{
		const FIntVector& Triangle = Triangles[TriangleId];
		for (int32 Corner = 0; Corner < TriangleSize; ++Corner)
		{
			if (Triangle[Corner] == From)
			{
				Ring.Emplace(Triangle[(Corner + 1) % TriangleSize], Triangle[(Corner + 2) % TriangleSize]);
				break;
			}
		}
	}
	return Ring.Num() == Incident.Num() && IsClosedRing(Ring);
}

bool FVoxelLandChaosMeshTopology::IsCompatibleGeometry(
	const FIntVector& Triangle, int32 From, int32 To, float Tolerance) const
{
	const FVector3f& A = Original.Vertices[Triangle.X].Position;
	const FVector3f& B = Original.Vertices[Triangle.Y].Position;
	const FVector3f& C = Original.Vertices[Triangle.Z].Position;
	const FVector3f Normal = FVector3f::CrossProduct(B - A, C - A).GetSafeNormal();

	if (FMath::Abs(FVector3f::DotProduct(Normal, Original.Vertices[To].Position - A)) > Tolerance)
	{
		return false;
	}

	const FIntVector Replacement = ReplaceVertex(Triangle, From, To);
	const FVector3f& NewA = Original.Vertices[Replacement.X].Position;
	const FVector3f& NewB = Original.Vertices[Replacement.Y].Position;
	const FVector3f& NewC = Original.Vertices[Replacement.Z].Position;
	const FVector3f NewNormal = FVector3f::CrossProduct(NewB - NewA, NewC - NewA).GetSafeNormal();
	return FVector3f::DotProduct(Normal, NewNormal) >= MinimumNormalDot;
}

bool FVoxelLandChaosMeshTopology::IsCoplanarFan(const FIntVector& Triangle, const FRing& Ring, float Tolerance) const
{
	const FVector3f& A = Original.Vertices[Triangle.X].Position;
	const FVector3f& B = Original.Vertices[Triangle.Y].Position;
	const FVector3f& C = Original.Vertices[Triangle.Z].Position;
	const FVector3f Normal = FVector3f::CrossProduct(B - A, C - A).GetSafeNormal();
	for (const FIntPoint& Link : Ring)
	{
		if (FMath::Abs(FVector3f::DotProduct(Normal, Original.Vertices[Link.X].Position - A)) > Tolerance)
		{
			return false;
		}
	}
	return true;
}

bool FVoxelLandChaosMeshTopology::IsCompatibleTarget(int32 From, int32 To, const FRing& Ring, float Tolerance) const
{
	int32 Previous = INDEX_NONE;
	int32 Next = INDEX_NONE;
	for (const FIntPoint& Link : Ring)
	{
		if (Link.Y == To)
		{
			Previous = Link.X;
		}
		if (Link.X == To)
		{
			Next = Link.Y;
		}
	}
	if (Previous == INDEX_NONE || Next == INDEX_NONE || Previous == Next)
	{
		return false;
	}

	// The edge endpoints may share only the two opposing vertices of the edge.
	// An extra shared neighbor would collapse a separate sheet or create duplicate faces.
	for (const int32 TriangleId : VertexTriangles[To])
	{
		const FIntVector& Triangle = Triangles[TriangleId];
		if (ContainsVertex(Triangle, Previous) && ContainsVertex(Triangle, Next))
		{
			return false;
		}
		for (const FIntPoint& Link : Ring)
		{
			const int32 Neighbor = Link.X;
			if (Neighbor != To && Neighbor != Previous && Neighbor != Next && ContainsVertex(Triangle, Neighbor))
			{
				return false;
			}
		}
	}

	for (const int32 TriangleId : VertexTriangles[From])
	{
		const FIntVector& Triangle = Triangles[TriangleId];
		// A removed face must remain nearly coplanar with the replacement fan.
		const bool bIsCompatible = ContainsVertex(Triangle, To) ? IsCoplanarFan(Triangle, Ring, Tolerance)
																: IsCompatibleGeometry(Triangle, From, To, Tolerance);
		if (!bIsCompatible)
		{
			return false;
		}
	}
	return true;
}

int32 FVoxelLandChaosMeshTopology::FindCollapseTarget(int32 From, int32 Stride, float Tolerance) const
{
	const int32 Rank = LatticeRank(Cells[From], Stride);
	if (Pinned[From] || Rank == 0)
	{
		return INDEX_NONE;
	}

	FRing Ring;
	if (!BuildRing(From, Ring))
	{
		return INDEX_NONE;
	}

	int32 Best = INDEX_NONE;
	int32 BestRank = Rank;
	float BestDistance = TNumericLimits<float>::Max();
	for (const FIntPoint& Link : Ring)
	{
		const int32 Candidate = Link.X;
		const int32 CandidateRank = LatticeRank(Cells[Candidate], Stride);
		if (Pinned[Candidate] || CandidateRank >= Rank || CandidateRank > BestRank
			|| VertexTriangles[Candidate].Num() > MaxFan)
		{
			continue;
		}
		const float Distance = (Original.Vertices[From].Position - Original.Vertices[Candidate].Position).SizeSquared();
		if (CandidateRank == BestRank && (Distance > BestDistance || (Distance == BestDistance && Candidate > Best)))
		{
			continue;
		}
		if (IsCompatibleTarget(From, Candidate, Ring, Tolerance))
		{
			Best = Candidate;
			BestRank = CandidateRank;
			BestDistance = Distance;
		}
	}
	return Best;
}

void FVoxelLandChaosMeshTopology::Collapse(int32 From, int32 To)
{
	const FIncidentTriangles Incident = MoveTemp(VertexTriangles[From]);
	for (const int32 TriangleId : Incident)
	{
		FIntVector& Triangle = Triangles[TriangleId];
		if (ContainsVertex(Triangle, To))
		{
			for (int32 Corner = 0; Corner < TriangleSize; ++Corner)
			{
				if (Triangle[Corner] != From)
				{
					VertexTriangles[Triangle[Corner]].RemoveSingleSwap(TriangleId, EAllowShrinking::No);
				}
			}
			Triangle = FIntVector(INDEX_NONE);
			--ActiveTriangles;
		}
		else
		{
			Triangle = ReplaceVertex(Triangle, From, To);
			VertexTriangles[To].Add(TriangleId);
		}
	}
	VertexTriangles[From].Reset();
}

bool FVoxelLandChaosMeshTopology::SimplifyPass(uint8 Lod, float VoxelSizeCm, TFunctionRef<bool()> IsCancelled)
{
	const int32 Stride = 1 << Lod;
	const float Tolerance = VoxelSizeCm * MaximumDeviationVoxels;
	for (int32 Vertex = 0; Vertex < Original.Vertices.Num(); ++Vertex)
	{
		if ((Vertex & PollMask) == 0 && IsCancelled())
		{
			return false;
		}
		const int32 Target = FindCollapseTarget(Vertex, Stride, Tolerance);
		if (Target != INDEX_NONE)
		{
			Collapse(Vertex, Target);
		}
	}
	return true;
}

bool FVoxelLandChaosMeshTopology::Simplify(TConstArrayView<int32> CellVertices, uint8 RequestedLod, float VoxelSizeCm,
	TFunctionRef<bool()> IsCancelled, uint8& OutEffectiveLod)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_ChaosMeshSimplification);
	Cells.SetNumUninitialized(Original.Vertices.Num());
	for (int32 Cell = 0; Cell < CellVertices.Num(); ++Cell)
	{
		if (CellVertices[Cell] != INDEX_NONE)
		{
			Cells[CellVertices[Cell]] =
				FIntVector(Cell % CellAxis, (Cell / CellAxis) % CellAxis, Cell / (CellAxis * CellAxis)) - FIntVector(1);
		}
	}
	if (!PinBoundaries(IsCancelled))
	{
		return false;
	}
	for (uint8 Lod = 1; Lod <= RequestedLod; ++Lod)
	{
		const int32 Before = ActiveTriangles;
		if (!SimplifyPass(Lod, VoxelSizeCm, IsCancelled))
		{
			return false;
		}
		if (ActiveTriangles < Before)
		{
			OutEffectiveLod = Lod;
		}
	}
	return !IsCancelled();
}

void FVoxelLandChaosMeshTopology::AppendVertex(
	const FVoxelLandMeshData& Source, int32 Old, TArray<int32>& Remap, FVoxelLandChaosMesh& Staged)
{
	int32& New = Remap[Old];
	if (New == INDEX_NONE)
	{
		const FVector3f Position = Source.Vertices[Old].Position;
		New = Staged.Vertices.Add(Position);
		Staged.Bounds += Position;
	}
	Staged.Indices.Add(static_cast<uint32>(New));
}

bool FVoxelLandChaosMeshTopology::ExportCanonical(
	const FVoxelLandMeshData& Source, FVoxelLandChaosMesh& Staged, TFunctionRef<bool()> IsCancelled)
{
	TArray<int32> Remap;
	Remap.Init(INDEX_NONE, Source.Vertices.Num());
	Staged.Vertices.Reserve(FMath::Min(Source.Vertices.Num(), Source.Indices.Num()));
	Staged.Indices.Reserve(Source.Indices.Num());
	for (int32 Index = 0; Index < Source.Indices.Num(); ++Index)
	{
		if ((Index & PollMask) == 0 && IsCancelled())
		{
			return false;
		}
		AppendVertex(Source, static_cast<int32>(Source.Indices[Index]), Remap, Staged);
	}
	return !IsCancelled();
}

bool FVoxelLandChaosMeshTopology::Export(FVoxelLandChaosMesh& Staged, TFunctionRef<bool()> IsCancelled) const
{
	TArray<int32> Remap;
	Remap.Init(INDEX_NONE, Original.Vertices.Num());
	Staged.Vertices.Reserve(FMath::Min(Original.Vertices.Num(), ActiveTriangles * TriangleSize));
	Staged.Indices.Reserve(ActiveTriangles * TriangleSize);
	for (int32 Index = 0; Index < Triangles.Num(); ++Index)
	{
		if ((Index & PollMask) == 0 && IsCancelled())
		{
			return false;
		}
		const FIntVector& Triangle = Triangles[Index];
		if (Triangle.X == INDEX_NONE)
		{
			continue;
		}
		for (int32 Corner = 0; Corner < TriangleSize; ++Corner)
		{
			AppendVertex(Original, Triangle[Corner], Remap, Staged);
		}
	}
	return !IsCancelled();
}

EVoxelLandMeshResult FVoxelLandChaosMeshing::Build(const FVoxelLandChunkData& Chunk, float VoxelSizeCm,
	uint8 RequestedLod, FVoxelLandChaosMesh& OutMesh, TFunctionRef<bool()> IsCancelled,
	bool bUnlimitedHeightCoordinates)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_ChaosExtractCollisionMesh);
	if (Chunk.Key.LOD != 0 || RequestedLod > MaxLod || Chunk.Density.Num() != FVoxelLandChunkData::SampleCount
		|| !VoxelLandMeshCoordinates::IsMeshKeyInPrecisionRange(Chunk.Key, VoxelSizeCm, bUnlimitedHeightCoordinates))
	{
		return EVoxelLandMeshResult::InvalidInput;
	}
	if (IsCancelled())
	{
		return EVoxelLandMeshResult::Cancelled;
	}

	FVoxelLandChaosMesh Staged;
	if (VoxelLandMeshing::Classify(Chunk) != EVoxelLandChunkClassification::Surface)
	{
		if (IsCancelled())
		{
			return EVoxelLandMeshResult::Cancelled;
		}
		OutMesh = MoveTemp(Staged);
		return EVoxelLandMeshResult::Success;
	}

	FVoxelLandMeshingScratch Scratch;
	FVoxelLandMeshData Surface;
	const EVoxelLandMeshResult Result = VoxelLandMeshing::BuildSurfaceNets(
		Chunk, VoxelSizeCm, Scratch, Surface, IsCancelled, bUnlimitedHeightCoordinates);
	if (Result != EVoxelLandMeshResult::Success)
	{
		return Result;
	}

	if (RequestedLod == 0 || Surface.Indices.IsEmpty())
	{
		if (!FVoxelLandChaosMeshTopology::ExportCanonical(Surface, Staged, IsCancelled))
		{
			return EVoxelLandMeshResult::Cancelled;
		}
	}
	else
	{
		FVoxelLandChaosMeshTopology Topology(Surface);
		if (!Topology.Simplify(Scratch.CellVertices, RequestedLod, VoxelSizeCm, IsCancelled, Staged.EffectiveLod)
			|| !Topology.Export(Staged, IsCancelled))
		{
			return EVoxelLandMeshResult::Cancelled;
		}
	}
	OutMesh = MoveTemp(Staged);
	return EVoxelLandMeshResult::Success;
}
