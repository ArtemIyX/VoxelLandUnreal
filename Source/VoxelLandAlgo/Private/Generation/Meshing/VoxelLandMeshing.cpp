#include "Generation/Meshing/VoxelLandMeshing.h"
#include "Generation/Clipmap/Seams/VoxelLandSeamNormalFrames.h"
#include "Generation/Clipmap/Seams/VoxelLandVerticalTerrainSeams.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

class FVoxelLandMeshingInternals final
{
public:
	static constexpr int32 CellCount = 34;
	static constexpr int32 Corner[8][3] = {
		{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}};
	static constexpr int32 Edges[12][2] = {
		{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
	static int32 SampleIndex(int32 X, int32 Y, int32 Z);
	static int32 CellIndex(int32 X, int32 Y, int32 Z);
	static int16 Sample(const FVoxelLandChunkData& Chunk, int32 X, int32 Y, int32 Z);
	static bool IsSolid(int16 Q);
	static EVoxelLandChunkClassification ClassifyFace(const FVoxelLandChunkData& Chunk, int32 Axis, bool bPositive);
	static FVector3f Gradient(const int16* Q, const FVector3f& P);
	static bool AddTriangle(
		const FVoxelLandMeshData& Mesh, TArray<uint32>& Indices, uint32 A, uint32 B, uint32 C, float Scale);
	static EVoxelLandMeshResult Build(const FVoxelLandChunkData& Chunk, float VoxelSizeCm,
		FVoxelLandMeshingScratch& Scratch, FVoxelLandMeshData& OutMesh, TFunctionRef<bool()> IsCancelled,
		bool bUnlimitedM4, FVoxelLandM7SeamMesh* Seam);
};

int32 FVoxelLandMeshingInternals::SampleIndex(int32 X, int32 Y, int32 Z)
{
	return (X + 1) + 35 * ((Y + 1) + 35 * (Z + 1));
}

int32 FVoxelLandMeshingInternals::CellIndex(int32 X, int32 Y, int32 Z)
{
	return (X + 1) + CellCount * ((Y + 1) + CellCount * (Z + 1));
}

int16 FVoxelLandMeshingInternals::Sample(const FVoxelLandChunkData& Chunk, int32 X, int32 Y, int32 Z)
{
	return Chunk.Density[SampleIndex(X, Y, Z)];
}

bool FVoxelLandMeshingInternals::IsSolid(int16 Q)
{
	return Q < 0;
}

FVector3f FVoxelLandMeshingInternals::Gradient(const int16* Q, const FVector3f& P)
{
	const float X = P.X, Y = P.Y, Z = P.Z;
	const float DX = (1 - Y) * (1 - Z) * (Q[1] - Q[0]) + Y * (1 - Z) * (Q[3] - Q[2]) + (1 - Y) * Z * (Q[5] - Q[4])
				   + Y * Z * (Q[7] - Q[6]);
	const float DY = (1 - X) * (1 - Z) * (Q[2] - Q[0]) + X * (1 - Z) * (Q[3] - Q[1]) + (1 - X) * Z * (Q[6] - Q[4])
				   + X * Z * (Q[7] - Q[5]);
	const float DZ = (1 - X) * (1 - Y) * (Q[4] - Q[0]) + X * (1 - Y) * (Q[5] - Q[1]) + (1 - X) * Y * (Q[6] - Q[2])
				   + X * Y * (Q[7] - Q[3]);
	return FVector3f(DX, DY, DZ);
}

bool FVoxelLandMeshingInternals::AddTriangle(
	const FVoxelLandMeshData& Mesh, TArray<uint32>& Indices, uint32 A, uint32 B, uint32 C, float Scale)
{
	if (A == B || B == C || C == A)
	{
		return false;
	}
	const FVector3f E1 = (Mesh.Vertices[B].Position - Mesh.Vertices[A].Position) / Scale;
	const FVector3f E2 = (Mesh.Vertices[C].Position - Mesh.Vertices[A].Position) / Scale;
	if (FVector3f::CrossProduct(E1, E2).SizeSquared() == 0.f)
	{
		return false;
	}
	Indices.Add(A);
	Indices.Add(B);
	Indices.Add(C);
	return true;
}

EVoxelLandChunkClassification VoxelLandMeshing::ClassifyBeforeSampling()
{
	return EVoxelLandChunkClassification::NeedsSamples;
}

EVoxelLandChunkClassification VoxelLandMeshing::Classify(const FVoxelLandChunkData& Chunk)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_Classify);
	if (Chunk.Density.Num() != FVoxelLandChunkData::SampleCount)
	{
		return EVoxelLandChunkClassification::NeedsSamples;
	}
	bool bSolid = false, bAir = false;
	for (const int16 Q : Chunk.Density)
	{
		bSolid |= FVoxelLandMeshingInternals::IsSolid(Q);
		bAir |= !FVoxelLandMeshingInternals::IsSolid(Q);
		if (bSolid && bAir)
		{
			return EVoxelLandChunkClassification::Surface;
		}
	}
	return bSolid ? EVoxelLandChunkClassification::Solid : EVoxelLandChunkClassification::Empty;
}

EVoxelLandMeshResult VoxelLandMeshing::BuildSurfaceNets(const FVoxelLandChunkData& Chunk, float VoxelSizeCm,
	FVoxelLandMeshingScratch& Scratch, FVoxelLandMeshData& OutMesh, TFunctionRef<bool()> IsCancelled)
{
	return BuildSurfaceNets(Chunk, VoxelSizeCm, Scratch, OutMesh, IsCancelled, false);
}

EVoxelLandMeshResult VoxelLandMeshing::BuildSurfaceNets(const FVoxelLandChunkData& Chunk, float VoxelSizeCm,
	FVoxelLandMeshingScratch& Scratch, FVoxelLandMeshData& OutMesh, TFunctionRef<bool()> IsCancelled, bool bUnlimitedM4)
{
	return FVoxelLandMeshingInternals::Build(Chunk, VoxelSizeCm, Scratch, OutMesh, IsCancelled, bUnlimitedM4, nullptr);
}

EVoxelLandChunkClassification FVoxelLandMeshingInternals::ClassifyFace(
	const FVoxelLandChunkData& Chunk, int32 Axis, bool bPositive)
{
	const int32 Plane = bPositive ? FVoxelLandChunkData::Cells : 0;
	const int32 U = (Axis + 1) % 3, V = (Axis + 2) % 3;
	int32 P[3] = {};
	P[Axis] = Plane;
	const bool bSolid = IsSolid(Sample(Chunk, P[0], P[1], P[2]));
	for (P[Axis] = Plane - 1; P[Axis] <= Plane + 1; ++P[Axis])
	{
		for (P[V] = -1; P[V] <= 33; ++P[V])
		{
			for (P[U] = -1; P[U] <= 33; ++P[U])
			{
				if (IsSolid(Sample(Chunk, P[0], P[1], P[2])) != bSolid)
				{
					return EVoxelLandChunkClassification::Surface;
				}
			}
		}
	}
	return bSolid ? EVoxelLandChunkClassification::Solid : EVoxelLandChunkClassification::Empty;
}

EVoxelLandMeshResult VoxelLandMeshing::BuildM7SeamMesh(const FVoxelLandChunkData& Chunk, int64 BaseVoxelSizeCm,
	FVoxelLandMeshingScratch& Scratch, FVoxelLandM7SeamMesh& OutMesh, TFunctionRef<bool()> IsCancelled,
	bool bUnlimitedM4)
{
	if (BaseVoxelSizeCm < 1 || BaseVoxelSizeCm > 134217
		|| !VoxelLandMeshCoordinates::IsMeshKeyInPrecisionRange(Chunk.Key, float(BaseVoxelSizeCm), bUnlimitedM4)
		|| Chunk.Density.Num() != FVoxelLandChunkData::SampleCount)
	{
		return EVoxelLandMeshResult::InvalidInput;
	}
	FVoxelLandM7SeamMesh Mesh;
	Mesh.Key = Chunk.Key;
	Mesh.BaseVoxelSizeCm = BaseVoxelSizeCm;
	Mesh.bUnlimitedM4 = bUnlimitedM4;
	Mesh.Classification = Classify(Chunk);
	if (Mesh.Classification == EVoxelLandChunkClassification::Surface)
	{
		const EVoxelLandMeshResult Result = FVoxelLandMeshingInternals::Build(
			Chunk, float(BaseVoxelSizeCm), Scratch, Mesh.Terrain, IsCancelled, bUnlimitedM4, &Mesh);
		if (Result != EVoxelLandMeshResult::Success)
		{
			return Result;
		}
	}
	for (int32 Face = 0; Face < UE_ARRAY_COUNT(Mesh.FaceSigns); ++Face)
	{
		if (IsCancelled())
		{
			return EVoxelLandMeshResult::Cancelled;
		}
		Mesh.FaceSigns[Face] = Mesh.Classification == EVoxelLandChunkClassification::Surface
								 ? FVoxelLandMeshingInternals::ClassifyFace(Chunk, Face / 2, (Face & 1) != 0)
								 : Mesh.Classification;
	}
	if (IsCancelled())
	{
		return EVoxelLandMeshResult::Cancelled;
	}
	OutMesh = MoveTemp(Mesh);
	return EVoxelLandMeshResult::Success;
}

EVoxelLandMeshResult FVoxelLandMeshingInternals::Build(const FVoxelLandChunkData& Chunk, float VoxelSizeCm,
	FVoxelLandMeshingScratch& Scratch, FVoxelLandMeshData& OutMesh, TFunctionRef<bool()> IsCancelled, bool bUnlimitedM4,
	FVoxelLandM7SeamMesh* Seam)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_SurfaceNets);
	float LevelSpacingCm = 0.f;
	if (!VoxelLandMeshCoordinates::TryGetLevelSpacing(VoxelSizeCm, Chunk.Key.LOD, LevelSpacingCm)
		|| !VoxelLandMeshCoordinates::IsMeshKeyInPrecisionRange(Chunk.Key, VoxelSizeCm, bUnlimitedM4)
		|| Chunk.Density.Num() != FVoxelLandChunkData::SampleCount)
	{
		return EVoxelLandMeshResult::InvalidInput;
	}
	FVoxelLandMeshData Mesh;
	const double Grid = VoxelLandMeshCoordinates::Grid(LevelSpacingCm);
	const FVector Origin = VoxelLandMeshCoordinates::RenderOrigin(Chunk.Key.Coordinate, LevelSpacingCm);
	Scratch.CellVertices.Init(INDEX_NONE, FVoxelLandMeshingInternals::CellCount * FVoxelLandMeshingInternals::CellCount
											  * FVoxelLandMeshingInternals::CellCount);
	for (int32 Z = -1; Z <= 32; ++Z)
	{
		if (IsCancelled())
		{
			return EVoxelLandMeshResult::Cancelled;
		}
		for (int32 Y = -1; Y <= 32; ++Y)
		{
			for (int32 X = -1; X <= 32; ++X)
			{
				int16 Q[8];
				bool bSolid = false, bAir = false;
				for (int32 I = 0; I < 8; ++I)
				{
					Q[I] = FVoxelLandMeshingInternals::Sample(Chunk, X + FVoxelLandMeshingInternals::Corner[I][0],
						Y + FVoxelLandMeshingInternals::Corner[I][1], Z + FVoxelLandMeshingInternals::Corner[I][2]);
					bSolid |= FVoxelLandMeshingInternals::IsSolid(Q[I]);
					bAir |= !FVoxelLandMeshingInternals::IsSolid(Q[I]);
				}
				if (!bSolid || !bAir)
				{
					continue;
				}
				FVector3f Sum = FVector3f::ZeroVector, Fallback = FVector3f::ZeroVector;
				int32 Crossings = 0;
				for (const auto& Edge : FVoxelLandMeshingInternals::Edges)
				{
					const int32 A = Edge[0], B = Edge[1];
					if (FVoxelLandMeshingInternals::IsSolid(Q[A]) == FVoxelLandMeshingInternals::IsSolid(Q[B]))
					{
						continue;
					}
					const float T = float(Q[A]) / float(int32(Q[A]) - int32(Q[B]));
					const FVector3f PA{float(FVoxelLandMeshingInternals::Corner[A][0]),
						float(FVoxelLandMeshingInternals::Corner[A][1]),
						float(FVoxelLandMeshingInternals::Corner[A][2])};
					const FVector3f PB{float(FVoxelLandMeshingInternals::Corner[B][0]),
						float(FVoxelLandMeshingInternals::Corner[B][1]),
						float(FVoxelLandMeshingInternals::Corner[B][2])};
					Sum += PA + (PB - PA) * T;
					++Crossings;
					Fallback += (PB - PA) * (Q[B] > Q[A] ? 1.f : -1.f);
				}
				const FVector3f P = Sum / float(Crossings);
				FVector3f N = FVoxelLandMeshingInternals::Gradient(Q, P).GetSafeNormal();
				if (N.IsNearlyZero())
				{
					++Mesh.NormalFallbacks;
					N = Fallback.GetSafeNormal();
					if (N.IsNearlyZero())
					{
						N = FVector3f::UpVector;
					}
				}
				FVoxelLandMeshVertex V;
				// Construct shared cell vertices in global integer-cell space, then snap
				// once to a scale-derived binary grid shared by all chunks.
				const int64 GX = int64(Chunk.Key.Coordinate.X) * 32 + X;
				const int64 GY = int64(Chunk.Key.Coordinate.Y) * 32 + Y;
				const int64 GZ = int64(Chunk.Key.Coordinate.Z) * 32 + Z;
				const FVector World(VoxelLandMeshCoordinates::Snap((double(GX) + P.X) * LevelSpacingCm, Grid),
					VoxelLandMeshCoordinates::Snap((double(GY) + P.Y) * LevelSpacingCm, Grid),
					VoxelLandMeshCoordinates::Snap((double(GZ) + P.Z) * LevelSpacingCm, Grid));
				V.Position = FVector3f(World - Origin);
				V.Normal = N;
				V.Tangent = FVoxelLandM7NormalFrames::TangentForNormal(N);
				V.UV = FVector2f(V.Position.X, V.Position.Y) / 100.f;
				Mesh.Bounds += V.Position;
				Scratch.CellVertices[FVoxelLandMeshingInternals::CellIndex(X, Y, Z)] = Mesh.Vertices.Add(V);
			}
		}
	}
	// Each edge is owned by the chunk containing its lower endpoint on all axes.
	// The optional positive layer is disjoint from owned edges and includes corners.
	const int32 OwnerLimit = Seam ? 33 : 32;
	for (int32 Z = 0; Z < OwnerLimit; ++Z)
	{
		if (IsCancelled())
		{
			return EVoxelLandMeshResult::Cancelled;
		}
		for (int32 Y = 0; Y < OwnerLimit; ++Y)
		{
			for (int32 X = 0; X < OwnerLimit; ++X)
			{
				const uint8 PositiveAxes = uint8((X == 32) | ((Y == 32) << 1) | ((Z == 32) << 2));
				TArray<uint32>& Indices = PositiveAxes ? Seam->PositiveOwnerIndices : Mesh.Indices;
				for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
				{
					int32 P[3] = {X, Y, Z};
					int32 End[3] = {X, Y, Z};
					++End[AxisIndex];
					const bool bStartSolid = FVoxelLandMeshingInternals::IsSolid(
						FVoxelLandMeshingInternals::Sample(Chunk, P[0], P[1], P[2]));
					if (bStartSolid
						== FVoxelLandMeshingInternals::IsSolid(
							FVoxelLandMeshingInternals::Sample(Chunk, End[0], End[1], End[2])))
					{
						continue;
					}
					const int32 U = (AxisIndex + 1) % 3, V = (AxisIndex + 2) % 3;
					constexpr int32 Offsets[4][2] = {{-1, -1}, {0, -1}, {0, 0}, {-1, 0}};
					uint32 Vertices[4];
					for (int32 I = 0; I < 4; ++I)
					{
						int32 C[3] = {X, Y, Z};
						C[U] += Offsets[I][0];
						C[V] += Offsets[I][1];
						const int32 Id = Scratch.CellVertices[FVoxelLandMeshingInternals::CellIndex(C[0], C[1], C[2])];
						if (Id == INDEX_NONE)
						{
							return EVoxelLandMeshResult::InvalidInput;
						}
						Vertices[I] = uint32(Id);
					}
					// Cyclic transverse axes give +axis; flip when density decreases along edge.
					if (!bStartSolid)
					{
						Swap(Vertices[1], Vertices[3]);
					}
					const uint32 OwnerEdge = uint32(3 * (X + 33 * (Y + 33 * Z)) + AxisIndex);
					if (AddTriangle(Mesh, Indices, Vertices[0], Vertices[1], Vertices[2], LevelSpacingCm)
						&& PositiveAxes)
					{
						Seam->PositiveOwnerEdges.Add(OwnerEdge);
					}
					if (AddTriangle(Mesh, Indices, Vertices[0], Vertices[2], Vertices[3], LevelSpacingCm)
						&& PositiveAxes)
					{
						Seam->PositiveOwnerEdges.Add(OwnerEdge);
					}
				}
			}
		}
	}
	OutMesh = MoveTemp(Mesh);
	return EVoxelLandMeshResult::Success;
}