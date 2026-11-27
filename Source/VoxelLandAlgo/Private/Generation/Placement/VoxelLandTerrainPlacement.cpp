#include "Generation/Placement/VoxelLandTerrainPlacement.h"
#include "Generation/Collision/VoxelLandCollisionField.h"
#include "Generation/Meshing/VoxelLandMeshing.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

bool FVoxelLandTerrainPlacement::BuildMeshes(const FVoxelLandTerrainPlacementInput& Input,
	TFunctionRef<bool()> IsCancelled, TArray<FVoxelLandTerrainPlacementMesh>& Out)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_PCGBuildCanonicalSurface);
	const float Size = static_cast<float>(Input.Field.GetVoxelSizeCm());
	FVoxelLandMeshingScratch Scratch;
	Out.Reserve(Input.Chunks.Num());
	for (const FVoxelLandCollisionField::FChunkPtr& Chunk : Input.Chunks)
	{
		if (IsCancelled())
		{
			return false;
		}
		if (VoxelLandMeshing::Classify(*Chunk) != EVoxelLandChunkClassification::Surface)
		{
			continue;
		}
		FVoxelLandTerrainPlacementMesh& Mesh = Out.AddDefaulted_GetRef();
		Mesh.Origin = VoxelLandMeshCoordinates::RenderOrigin(Chunk->Key.Coordinate, Size);
		const bool bExtendedRange =
			VoxelLandMeshCoordinates::IsUnlimitedM4KeyInPrecisionRange(Chunk->Key.Coordinate, Size);
		if (VoxelLandMeshing::BuildSurfaceNets(*Chunk, Size, Scratch, Mesh.Geometry, IsCancelled, bExtendedRange)
			!= EVoxelLandMeshResult::Success)
		{
			return false;
		}
	}
	return true;
}

uint64 FVoxelLandTerrainPlacement::Mix(uint64 Value)
{
	Value = (Value ^ (Value >> 30)) * 0xbf58476d1ce4e5b9ull;
	Value = (Value ^ (Value >> 27)) * 0x94d049bb133111ebull;
	return Value ^ (Value >> 31);
}

FVector2d FVoxelLandTerrainPlacement::GridPosition(const FInt64Point& Key, double CellWidth, uint64 Seed)
{
	const uint64 KeyHash = Mix(static_cast<uint64>(Key.X) ^ Mix(static_cast<uint64>(Key.Y)) ^ Seed);
	constexpr double UintRange = 4294967296.0;
	constexpr double Offset = 0.25;
	constexpr double Jitter = 0.5;
	const double U = static_cast<double>(static_cast<uint32>(KeyHash)) / UintRange;
	const double V = static_cast<double>(static_cast<uint32>(KeyHash >> 32)) / UintRange;
	return CellWidth
		 * (FVector2d(static_cast<double>(Key.X), static_cast<double>(Key.Y))
			 + FVector2d(Offset + Jitter * U, Offset + Jitter * V));
}

bool FVoxelLandTerrainPlacement::KeyLess(const FInt64Point& A, const FInt64Point& B)
{
	return A.Y != B.Y ? A.Y < B.Y : A.X < B.X;
}

FBox FVoxelLandTerrainPlacement::BoundsFor(const FVoxelLandTerrainPlacementMesh& Mesh)
{
	return FBox(FVector(Mesh.Geometry.Bounds.Min) + Mesh.Origin, FVector(Mesh.Geometry.Bounds.Max) + Mesh.Origin);
}

FVector FVoxelLandTerrainPlacement::InterpolateNormal(const FVoxelLandMeshData& Mesh, int32 Index, double U, double V)
{
	const FVector A(Mesh.Vertices[Mesh.Indices[Index]].Normal);
	const FVector B(Mesh.Vertices[Mesh.Indices[Index + 1]].Normal);
	const FVector C(Mesh.Vertices[Mesh.Indices[Index + 2]].Normal);
	return (A + (B - A) * U + (C - A) * V).GetSafeNormal();
}

void FVoxelLandTerrainPlacement::AccumulateTriangle(const FVoxelLandTerrainPlacementMesh& Mesh, int32 Triangle,
	const FBox& Bounds, double CellWidth, uint64 Seed, double MinimumNormalZ,
	TMap<FInt64Point, FVoxelLandTerrainPlacementPoint>& Points)
{
	const FVoxelLandMeshData& Geometry = Mesh.Geometry;
	const FVector A = Mesh.Origin + FVector(Geometry.Vertices[Geometry.Indices[Triangle]].Position);
	const FVector B = Mesh.Origin + FVector(Geometry.Vertices[Geometry.Indices[Triangle + 1]].Position);
	const FVector C = Mesh.Origin + FVector(Geometry.Vertices[Geometry.Indices[Triangle + 2]].Position);
	const FVector AB = B - A;
	const FVector AC = C - A;
	const double Det = AB.X * AC.Y - AB.Y * AC.X;
	if (FMath::IsNearlyZero(Det, UE_DOUBLE_SMALL_NUMBER))
	{
		return;
	}
	const double MinX = FMath::Max(Bounds.Min.X, FMath::Min3(A.X, B.X, C.X));
	const double MinY = FMath::Max(Bounds.Min.Y, FMath::Min3(A.Y, B.Y, C.Y));
	const double MaxX = FMath::Min(Bounds.Max.X, FMath::Max3(A.X, B.X, C.X));
	const double MaxY = FMath::Min(Bounds.Max.Y, FMath::Max3(A.Y, B.Y, C.Y));
	if (MinX > MaxX || MinY > MaxY)
	{
		return;
	}
	const FInt64Point First(FMath::FloorToInt64(MinX / CellWidth), FMath::FloorToInt64(MinY / CellWidth));
	const FInt64Point Last(FMath::FloorToInt64(MaxX / CellWidth), FMath::FloorToInt64(MaxY / CellWidth));
	for (int64 Y = First.Y; Y <= Last.Y; ++Y)
	{
		for (int64 X = First.X; X <= Last.X; ++X)
		{
			const FInt64Point Key(X, Y);
			const FVector2d XY = GridPosition(Key, CellWidth, Seed);
			if (XY.X < Bounds.Min.X || XY.X >= Bounds.Max.X || XY.Y < Bounds.Min.Y || XY.Y >= Bounds.Max.Y)
			{
				continue;
			}
			const FVector2d Offset(XY.X - A.X, XY.Y - A.Y);
			const double U = (Offset.X * AC.Y - Offset.Y * AC.X) / Det;
			const double V = (AB.X * Offset.Y - AB.Y * Offset.X) / Det;
			if (U < 0.0 || V < 0.0 || U + V > 1.0)
			{
				continue;
			}
			const double Z = A.Z + AB.Z * U + AC.Z * V;
			const FVector Normal = InterpolateNormal(Geometry, Triangle, U, V);
			if (Z < Bounds.Min.Z || Z >= Bounds.Max.Z || Normal.Z < MinimumNormalZ)
			{
				continue;
			}
			FVoxelLandTerrainPlacementPoint* Existing = Points.Find(Key);
			if (!Existing || Z > Existing->Position.Z)
			{
				const uint32 PointSeed = static_cast<uint32>(
					Mix(Seed ^ Mix(static_cast<uint64>(X)) ^ Mix(static_cast<uint64>(Y) + 0x9e3779b97f4a7c15ull)));
				Points.Add(Key, {FVector(XY.X, XY.Y, Z), Normal, PointSeed});
			}
		}
	}
}

void FVoxelLandTerrainPlacement::AccumulateMesh(const FVoxelLandTerrainPlacementMesh& Mesh, const FBox& Bounds,
	double CellWidth, uint64 Seed, double MinimumNormalZ, TMap<FInt64Point, FVoxelLandTerrainPlacementPoint>& Points)
{
	if (Mesh.Geometry.Indices.IsEmpty() || !Bounds.Intersect(BoundsFor(Mesh)))
	{
		return;
	}
	for (int32 Index = 0; Index < Mesh.Geometry.Indices.Num(); Index += 3)
	{
		AccumulateTriangle(Mesh, Index, Bounds, CellWidth, Seed, MinimumNormalZ, Points);
	}
}

bool FVoxelLandTerrainPlacement::Sample(const FVoxelLandTerrainPlacementInput& Input, TFunctionRef<bool()> IsCancelled,
	double SpacingCm, int32 Seed, double MaximumSlopeDegrees, TArray<FVoxelLandTerrainPlacementPoint>& Out)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_PCGSampleSurface);
	if (!FMath::IsFinite(SpacingCm) || SpacingCm < MinimumSpacingCm || SpacingCm > MaximumSpacingCm
		|| !FMath::IsFinite(MaximumSlopeDegrees) || MaximumSlopeDegrees < 0.0 || MaximumSlopeDegrees >= 90.0)
	{
		return false;
	}
	const FBox& Bounds = Input.Bounds;
	// The central half of each grid cell is used for jitter, guaranteeing the
	// requested minimum horizontal spacing even across execution cell boundaries.
	const double CellWidth = SpacingCm * 2.0;
	const FVector Size = Bounds.GetSize();
	const double Columns = FMath::CeilToDouble(Size.X / CellWidth) + 1.0;
	const double Rows = FMath::CeilToDouble(Size.Y / CellWidth) + 1.0;
	if (!Bounds.IsValid || Columns <= 0.0 || Rows <= 0.0 || Columns * Rows > MaxPoints)
	{
		return false;
	}
	TArray<FVoxelLandTerrainPlacementMesh> Meshes;
	if (!BuildMeshes(Input, IsCancelled, Meshes))
	{
		return false;
	}
	const double MinimumNormalZ = FMath::Cos(FMath::DegreesToRadians(MaximumSlopeDegrees));
	TMap<FInt64Point, FVoxelLandTerrainPlacementPoint> Points;
	Points.Reserve(static_cast<int32>(Columns * Rows));
	for (const FVoxelLandTerrainPlacementMesh& Mesh : Meshes)
	{
		if (IsCancelled())
		{
			return false;
		}
		AccumulateMesh(Mesh, Bounds, CellWidth, static_cast<uint64>(Seed), MinimumNormalZ, Points);
	}
	TArray<FInt64Point> Keys;
	Points.GetKeys(Keys);
	Keys.Sort(&FVoxelLandTerrainPlacement::KeyLess);
	Out.Reserve(Keys.Num());
	for (const FInt64Point& Key : Keys)
	{
		Out.Add(Points.FindChecked(Key));
	}
	return true;
}

bool FVoxelLandTerrainPlacement::Project(TConstArrayView<FVoxelLandTerrainPlacementMesh> Meshes,
	const FVector& LogicalStart, const FVector& Direction, double DistanceCm, bool bGroundOnly,
	FVoxelLandTerrainPlacementPoint& Out)
{
	const FVector Segment = Direction * DistanceCm;
	const FVector End = LogicalStart + Segment;
	double Closest = DistanceCm;
	bool bFound = false;
	for (const FVoxelLandTerrainPlacementMesh& Mesh : Meshes)
	{
		const FVoxelLandMeshData& Geometry = Mesh.Geometry;
		if (Geometry.Indices.IsEmpty() || !FMath::LineBoxIntersection(BoundsFor(Mesh), LogicalStart, End, Segment))
		{
			continue;
		}
		const FVector Start = LogicalStart - Mesh.Origin;
		for (int32 Index = 0; Index < Geometry.Indices.Num(); Index += 3)
		{
			const FVector A(Geometry.Vertices[Geometry.Indices[Index]].Position);
			const FVector AB = FVector(Geometry.Vertices[Geometry.Indices[Index + 1]].Position) - A;
			const FVector AC = FVector(Geometry.Vertices[Geometry.Indices[Index + 2]].Position) - A;
			const FVector D = FVector::CrossProduct(Direction, AC);
			const double Determinant = FVector::DotProduct(AB, D);
			if (FMath::IsNearlyZero(Determinant, UE_DOUBLE_SMALL_NUMBER))
			{
				continue;
			}
			const FVector T = Start - A;
			const FVector Q = FVector::CrossProduct(T, AB);
			const double U = FVector::DotProduct(T, D) / Determinant;
			const double V = FVector::DotProduct(Direction, Q) / Determinant;
			const double Distance = FVector::DotProduct(AC, Q) / Determinant;
			if (U < 0.0 || V < 0.0 || U + V > 1.0 || Distance < 0.0 || Distance > Closest)
			{
				continue;
			}
			const FVector Normal = InterpolateNormal(Geometry, Index, U, V);
			if (!bGroundOnly || Normal.Z > 0.0)
			{
				Out.Position = LogicalStart + Direction * Distance;
				Out.Normal = Normal;
				Closest = Distance;
				bFound = true;
			}
		}
	}
	return bFound;
}

bool FVoxelLandTerrainPlacement::CanProject(
	const FVoxelLandCollisionField& Field, const FVector& LogicalStart, const FVector& Direction, double DistanceCm)
{
	FInt64Vector Cell;
	if (!Field.TryGetCell(LogicalStart, Cell))
	{
		return false;
	}
	FVector Next(DBL_MAX);
	FVector Advance(DBL_MAX);
	FInt64Vector Step(0);
	const double VoxelSize = Field.GetVoxelSizeCm();
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (Direction[Axis] != 0.0)
		{
			Step[Axis] = Direction[Axis] > 0.0 ? 1 : -1;
			const int64 Boundary = Cell[Axis] + (Step[Axis] > 0 ? 1 : 0);
			Next[Axis] = (VoxelSize * static_cast<double>(Boundary) - LogicalStart[Axis]) / Direction[Axis];
			Advance[Axis] = VoxelSize / FMath::Abs(Direction[Axis]);
		}
	}
	constexpr int32 MaxVisitedCells = 8192;
	for (int32 Visited = 0; Visited < MaxVisitedCells; ++Visited)
	{
		double Density[2][2][2];
		if (Field.ReadCell(Cell, Density) != EVoxelLandCollisionFieldStatus::Available)
		{
			return false;
		}
		const double Boundary = Next.GetMin();
		if (Boundary >= DistanceCm)
		{
			return true;
		}
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Next[Axis] == Boundary)
			{
				Cell[Axis] += Step[Axis];
				Next[Axis] += Advance[Axis];
			}
		}
	}
	return false;
}
