#include "Generation/Clipmap/Seams/VoxelLandVerticalTerrainSeams.h"

#include "Generation/Clipmap/Seams/VoxelLandSeamNormalFrames.h"

namespace
{
constexpr int32 MaxInputVertices = 34 * 34 * 34;
constexpr int32 MaxOwnedIndices = 32 * 32 * 32 * 18;
constexpr int32 MaxCandidateTriangles = (33 * 33 * 33 - 32 * 32 * 32) * 6;
constexpr int32 MaxClipPolygons = 4096;
constexpr int32 MaxNewVertices = 16384;
constexpr int32 MaxTraceSegments = 512;
constexpr int32 MaxKnots = FVoxelLandM7NormalFrames::MaxAnchorsPerBatch;

struct FCut
{
	uint8 Axis = 0;
	uint8 Across = 1;
	int32 CoarseSide = -1;
	FVector Origin = FVector::ZeroVector;
	double Width = 0.0;
	double Grid = 0.0;
	double Clearance = 0.0;
	double DepthCap = 0.0;
	double BaseSpacing = 0.0;

	// Patch space is (normal offset, horizontal face parameter, relative height).
	FVector ToPatch(const FVector& Logical) const
	{
		const FVector Relative = Logical - Origin;
		return {Relative[Axis], Relative[Across], Relative.Z};
	}

	FVector ToLogical(const FVector& Patch) const
	{
		FVector Logical = Origin;
		Logical[Axis] += Patch.X;
		Logical[Across] += Patch.Y;
		Logical.Z += Patch.Z;
		return Logical;
	}

	double Snap(double Value) const { return VoxelLandMeshCoordinates::Snap(Value, Grid); }
};

struct FPoint
{
	FVector P = FVector::ZeroVector;
	FVoxelLandMeshVertex Vertex;
	int32 Source = INDEX_NONE;
};

using FPolygon = TArray<FPoint, TInlineAllocator<12>>;

struct FSegment
{
	FVector2d Start;
	FVector2d End;

	double HeightAt(double U) const { return FMath::Lerp(Start.Y, End.Y, (U - Start.X) / (End.X - Start.X)); }
};

struct FKnot
{
	double U = 0.0;
	double CoarseZ = 0.0;
	double FineZ = 0.0;
};

struct FParticipant
{
	const FVoxelLandM7SeamMesh* Source = nullptr;
	FVector Origin = FVector::ZeroVector;
	double Spacing = 0.0;
	int32 Side = -1;
	uint8 AllowedPositiveOwners = 0;
	FVoxelLandMeshData Mesh;
	TArray<FPolygon> RimPolygons;
};

FVoxelLandError Reject(const TCHAR* Why)
{
	return {EVoxelLandErrorCode::InvalidParameter, Why};
}

FVoxelLandError Cancel()
{
	return {EVoxelLandErrorCode::Cancelled, TEXT("M7 vertical seam cancelled")};
}

bool ValidSource(const FVoxelLandM7SeamMesh& Source, uint8 LOD, int64 BaseSpacing, bool bUnlimitedM4)
{
	const FVoxelLandMeshData& Mesh = Source.Terrain;
	return Source.Key.LOD == LOD && Source.BaseVoxelSizeCm == BaseSpacing && Source.bUnlimitedM4 == bUnlimitedM4
		&& VoxelLandMeshCoordinates::IsMeshKeyInPrecisionRange(Source.Key, float(BaseSpacing), bUnlimitedM4)
		&& Source.Classification != EVoxelLandChunkClassification::NeedsSamples
		&& Mesh.Vertices.Num() <= MaxInputVertices && Mesh.Indices.Num() <= MaxOwnedIndices
		&& Mesh.Indices.Num() % 3 == 0 && Source.PositiveOwnerEdges.Num() <= MaxCandidateTriangles
		&& Source.PositiveOwnerIndices.Num() == 3 * Source.PositiveOwnerEdges.Num();
}

FPoint Interpolate(const FPoint& A, const FPoint& B, double T)
{
	if (T == 0.0)
	{
		return A;
	}
	if (T == 1.0)
	{
		return B;
	}
	FPoint Result;
	Result.P = FMath::Lerp(A.P, B.P, T);
	Result.Vertex = A.Vertex;
	Result.Vertex.Normal = FMath::Lerp(A.Vertex.Normal, B.Vertex.Normal, float(T)).GetSafeNormal();
	if (Result.Vertex.Normal.IsNearlyZero())
	{
		Result.Vertex.Normal = A.Vertex.Normal;
	}
	Result.Vertex.Tangent = FVoxelLandM7NormalFrames::TangentForNormal(Result.Vertex.Normal);
	Result.Vertex.UV = FMath::Lerp(A.Vertex.UV, B.Vertex.UV, float(T));
	return Result;
}

void AddDistinct(FPolygon& Polygon, const FPoint& Point)
{
	if (Polygon.IsEmpty() || Polygon.Last().P != Point.P)
	{
		Polygon.Add(Point);
	}
}

FPolygon CutPolygon(const FPolygon& Polygon, int32 Axis, double At, bool bKeepGreater)
{
	FPolygon Result;
	if (Polygon.IsEmpty())
	{
		return Result;
	}
	for (int32 Index = 0; Index < Polygon.Num(); ++Index)
	{
		const FPoint& A = Polygon[Index];
		const FPoint& B = Polygon[(Index + 1) % Polygon.Num()];
		const double AValue = A.P[Axis] - At;
		const double BValue = B.P[Axis] - At;
		if (bKeepGreater ? AValue >= 0.0 : AValue <= 0.0)
		{
			AddDistinct(Result, A);
		}
		if ((AValue < 0.0 && BValue > 0.0) || (AValue > 0.0 && BValue < 0.0))
		{
			// Order the endpoints independently of triangle winding so shared edges
			// yield the same intersection before the final common rim snap.
			FPoint Point = AValue < BValue ? Interpolate(A, B, -AValue / (BValue - AValue))
										   : Interpolate(B, A, -BValue / (AValue - BValue));
			Point.P[Axis] = At;
			AddDistinct(Result, Point);
		}
	}
	if (Result.Num() > 1 && Result[0].P == Result.Last().P)
	{
		Result.Pop(EAllowShrinking::No);
	}
	return Result;
}

int32 AddPoint(FParticipant& Work, const FCut& Cut, const FPoint& Point)
{
	if (Point.Source != INDEX_NONE)
	{
		return Point.Source;
	}
	FVoxelLandMeshVertex Vertex = Point.Vertex;
	Vertex.Position = FVector3f(Cut.ToLogical(Point.P) - Work.Origin);
	Work.Mesh.Bounds += Vertex.Position;
	return Work.Mesh.Vertices.Add(Vertex);
}

bool EmitPolygon(FParticipant& Work, const FCut& Cut, const FPolygon& Polygon)
{
	if (Polygon.Num() < 3)
	{
		return true;
	}
	if (Work.Mesh.Vertices.Num() + Polygon.Num() > Work.Source->Terrain.Vertices.Num() + MaxNewVertices
		|| Work.Mesh.Indices.Num() + 3 * (Polygon.Num() - 2) > MaxOwnedIndices + 3 * MaxNewVertices)
	{
		return false;
	}
	// A rim may contain many collinear vertices. Fan from off the face so the
	// emitted triangles retain each shared break along the boundary.
	int32 Pivot = 0;
	while (Pivot + 1 < Polygon.Num() && Polygon[Pivot].P.X == 0.0)
	{
		++Pivot;
	}
	const int32 PivotId = AddPoint(Work, Cut, Polygon[Pivot]);
	int32 Previous = AddPoint(Work, Cut, Polygon[(Pivot + 1) % Polygon.Num()]);
	for (int32 Index = 2; Index < Polygon.Num(); ++Index)
	{
		const int32 Current = AddPoint(Work, Cut, Polygon[(Pivot + Index) % Polygon.Num()]);
		const FVector3f A = Work.Mesh.Vertices[Previous].Position - Work.Mesh.Vertices[PivotId].Position;
		const FVector3f B = Work.Mesh.Vertices[Current].Position - Work.Mesh.Vertices[PivotId].Position;
		if (FVector3f::CrossProduct(A, B).SizeSquared() != 0.f)
		{
			Work.Mesh.Indices.Add(uint32(PivotId));
			Work.Mesh.Indices.Add(uint32(Previous));
			Work.Mesh.Indices.Add(uint32(Current));
		}
		Previous = Current;
	}
	return true;
}

bool ReadTriangle(const FParticipant& Work, const FCut& Cut, TConstArrayView<uint32> Indices, FPolygon& Out)
{
	for (uint32 Index : Indices)
	{
		if (Index >= uint32(Work.Source->Terrain.Vertices.Num()))
		{
			return false;
		}
		const FVoxelLandMeshVertex& Vertex = Work.Source->Terrain.Vertices[Index];
		if (Vertex.Position.ContainsNaN() || Vertex.Position.GetAbsMax() > 34.0 * Work.Spacing
			|| Vertex.Normal.ContainsNaN() || Vertex.Tangent.ContainsNaN())
		{
			return false;
		}
		Out.Add({Cut.ToPatch(Work.Origin + FVector(Vertex.Position)), Vertex, int32(Index)});
	}
	return true;
}

bool TouchesFace(const FPolygon& Triangle)
{
	const double A = Triangle[0].P.X, B = Triangle[1].P.X, C = Triangle[2].P.X;
	return FMath::Min3(A, B, C) <= 0.0 && FMath::Max3(A, B, C) >= 0.0;
}

bool ClipTriangle(FParticipant& Work, const FCut& Cut, FPolygon Triangle, bool bOwned, TArray<FSegment>& Trace)
{
	// Preserve owned geometry outside the active rectangle, including ordinary
	// overlap at adjacent same-level faces. Candidate geometry adds no wings.
	for (int32 Axis = 1; Axis <= 2; ++Axis)
	{
		for (int32 Edge = 0; Edge < 2; ++Edge)
		{
			const double At = Edge == 0 ? 0.0 : Cut.Width;
			const bool bGreater = Edge == 0;
			const bool bHasOutside = Triangle.ContainsByPredicate(
				[=](const FPoint& Point) { return bGreater ? Point.P[Axis] < At : Point.P[Axis] > At; });
			if (bOwned && bHasOutside && !EmitPolygon(Work, Cut, CutPolygon(Triangle, Axis, At, !bGreater)))
			{
				return false;
			}
			Triangle = CutPolygon(Triangle, Axis, At, bGreater);
			if (Triangle.Num() < 3)
			{
				return true;
			}
		}
	}
	Triangle = CutPolygon(Triangle, 0, 0.0, Work.Side > 0);
	if (Triangle.Num() < 3)
	{
		return true;
	}
	bool bOnFace = false;
	bool bOffFace = false;
	for (FPoint& Point : Triangle)
	{
		if (Point.P.X == 0.0)
		{
			bOnFace = true;
			Point.P.Y = Cut.Snap(Point.P.Y);
			Point.P.Z = Cut.Snap(Point.P.Z);
			Point.Source = INDEX_NONE;
		}
		else
		{
			bOffFace = true;
		}
	}
	if (!bOnFace)
	{
		return EmitPolygon(Work, Cut, Triangle);
	}
	if (!bOffFace)
	{
		// Ignore a touch that collapses to an edge. A polygon with area lying
		// in the interface is a wall, not a top trace.
		for (int32 Index = 2; Index < Triangle.Num(); ++Index)
		{
			const FVector A = Triangle[Index - 1].P - Triangle[0].P;
			const FVector B = Triangle[Index].P - Triangle[0].P;
			if (FVector::CrossProduct(A, B).SizeSquared() != 0.0)
			{
				return false;
			}
		}
		return true;
	}
	if (Work.RimPolygons.Num() >= MaxClipPolygons)
	{
		return false;
	}
	for (int32 Index = 0; Index < Triangle.Num(); ++Index)
	{
		const FVector& A = Triangle[Index].P;
		const FVector& B = Triangle[(Index + 1) % Triangle.Num()].P;
		if (A.X != 0.0 || B.X != 0.0 || A == B)
		{
			continue;
		}
		FSegment Segment{{A.Y, A.Z}, {B.Y, B.Z}};
		if (Segment.Start.X > Segment.End.X)
		{
			Swap(Segment.Start, Segment.End);
		}
		const double Run = Segment.End.X - Segment.Start.X;
		if (Run <= 0.0 || FMath::Abs(Segment.End.Y - Segment.Start.Y) > 4.0 * Run || Trace.Num() >= MaxTraceSegments)
		{
			return false;
		}
		Trace.Add(Segment);
	}
	Work.RimPolygons.Add(MoveTemp(Triangle));
	return true;
}

FVoxelLandError PrepareParticipant(
	FParticipant& Work, const FCut& Cut, TArray<FSegment>& Trace, TFunctionRef<bool()> IsCancelled)
{
	const FVoxelLandM7SeamMesh& Source = *Work.Source;
	Work.Mesh.Vertices = Source.Terrain.Vertices;
	Work.Mesh.Bounds = Source.Terrain.Bounds;
	Work.Mesh.NormalFallbacks = Source.Terrain.NormalFallbacks;
	Work.Mesh.Indices.Reserve(Source.Terrain.Indices.Num());
	const TConstArrayView<uint32> Owned(Source.Terrain.Indices);
	for (int32 Index = 0; Index < Owned.Num(); Index += 3)
	{
		if ((Index % (3 * 256)) == 0 && IsCancelled())
		{
			return Cancel();
		}
		FPolygon Triangle;
		const TConstArrayView<uint32> Face = Owned.Slice(Index, 3);
		if (!ReadTriangle(Work, Cut, Face, Triangle))
		{
			return Reject(TEXT("Invalid owned triangle in M7 seam source"));
		}
		if (Triangle[0].P.X * Work.Side > 0.0 && Triangle[1].P.X * Work.Side > 0.0 && Triangle[2].P.X * Work.Side > 0.0)
		{
			Work.Mesh.Indices.Append(Face.GetData(), Face.Num());
		}
		else if (!ClipTriangle(Work, Cut, MoveTemp(Triangle), true, Trace))
		{
			return Reject(TEXT("Unsupported owned vertical rim geometry or clip budget"));
		}
	}

	const TConstArrayView<uint32> Candidates(Source.PositiveOwnerIndices);
	const TArray<uint32>& Edges = Source.PositiveOwnerEdges;
	for (int32 Index = 0; Index < Edges.Num();)
	{
		if (IsCancelled())
		{
			return Cancel();
		}
		const uint32 Edge = Edges[Index];
		if (Edge >= uint32(3 * 33 * 33 * 33) || (Index > 0 && Edge <= Edges[Index - 1]))
		{
			return Reject(TEXT("Noncanonical positive-owner seam candidates"));
		}
		const uint32 Point = Edge / 3;
		const uint8 Axes =
			uint8((Point % 33 == 32) | ((Point / 33 % 33 == 32) << 1) | ((Point / (33 * 33) == 32) << 2));
		if (Axes == 0)
		{
			return Reject(TEXT("M7 seam candidate collides with an ordinary owner"));
		}
		const int32 First = Index++;
		if (Index < Edges.Num() && Edges[Index] == Edge)
		{
			++Index;
		}
		if ((Axes & Work.AllowedPositiveOwners) != Axes)
		{
			continue;
		}
		FPolygon Halves[2];
		bool bTouches = false;
		for (int32 Half = 0; Half < Index - First; ++Half)
		{
			if (!ReadTriangle(Work, Cut, Candidates.Slice(3 * (First + Half), 3), Halves[Half]))
			{
				return Reject(TEXT("Invalid positive-owner triangle in M7 seam source"));
			}
			bTouches |= TouchesFace(Halves[Half]);
		}
		if (bTouches)
		{
			for (int32 Half = 0; Half < Index - First; ++Half)
			{
				if (!ClipTriangle(Work, Cut, MoveTemp(Halves[Half]), false, Trace))
				{
					return Reject(TEXT("Unsupported candidate vertical rim geometry or clip budget"));
				}
			}
		}
	}
	return {};
}

bool SortCompleteTrace(TArray<FSegment>& Trace, const FCut& Cut)
{
	Trace.Sort([](const FSegment& A, const FSegment& B) { return A.Start.X < B.Start.X; });
	if (Trace.IsEmpty() || Trace[0].Start.X != 0.0 || Trace.Last().End.X != Cut.Width)
	{
		return false;
	}
	for (int32 Index = 1; Index < Trace.Num(); ++Index)
	{
		// Exact on the common binary grid. Any overlap, gap or height step is
		// ambiguous; selecting one branch would leave an uncovered cut.
		if (Trace[Index - 1].End != Trace[Index].Start)
		{
			return false;
		}
	}
	return true;
}

template <typename T, typename Projection> int32 FindLower(const TArray<T>& Entries, double U, Projection Project)
{
	int32 Low = 0;
	int32 High = Entries.Num();
	while (Low < High)
	{
		const int32 Middle = Low + (High - Low) / 2;
		if (Project(Entries[Middle]) < U)
		{
			Low = Middle + 1;
		}
		else
		{
			High = Middle;
		}
	}
	return Low;
}

double TraceHeight(const TArray<FSegment>& Trace, double U)
{
	const int32 Index = FindLower(Trace, U, [](const FSegment& Segment) { return Segment.End.X; });
	return Trace[Index].HeightAt(U);
}

void AddTraceKnots(const TArray<FSegment>& Trace, const FCut& Cut, TArray<double>& Parameters)
{
	const double Middle = Cut.Width * 0.5;
	for (const FSegment& Segment : Trace)
	{
		Parameters.Add(Segment.End.X);
		if ((Segment.Start.Y < Middle && Segment.End.Y > Middle)
			|| (Segment.Start.Y > Middle && Segment.End.Y < Middle))
		{
			const double T = (Middle - Segment.Start.Y) / (Segment.End.Y - Segment.Start.Y);
			Parameters.Add(Cut.Snap(FMath::Lerp(Segment.Start.X, Segment.End.X, T)));
		}
	}
}

bool MakeKnots(const TArray<FSegment>& Coarse, const TArray<FSegment>& Fine, const FCut& Cut, TArray<FKnot>& Knots)
{
	TArray<double> Parameters{0.0, Cut.Width * 0.5};
	AddTraceKnots(Coarse, Cut, Parameters);
	AddTraceKnots(Fine, Cut, Parameters);
	Parameters.Sort();
	FKnot Previous;
	double PreviousCoarse = 0.0, PreviousFine = 0.0;
	for (const double U : Parameters)
	{
		if (!Knots.IsEmpty() && U == Previous.U)
		{
			continue;
		}
		const double CoarseZ = TraceHeight(Coarse, U);
		const double FineZ = TraceHeight(Fine, U);
		FKnot Current{U, Cut.Snap(CoarseZ), Cut.Snap(FineZ)};
		const double Gap = CoarseZ - FineZ;
		const double PreviousGap = PreviousCoarse - PreviousFine;
		if (!Knots.IsEmpty() && ((PreviousGap < 0.0 && Gap > 0.0) || (PreviousGap > 0.0 && Gap < 0.0)))
		{
			const double T = PreviousGap / (PreviousGap - Gap);
			const double CrossingU = Cut.Snap(FMath::Lerp(Previous.U, U, T));
			const double CrossingZ = Cut.Snap(FMath::Lerp(PreviousCoarse, CoarseZ, T));
			// Both traces use one quantized crossing. If it rounds onto an existing
			// knot, eliminate the sub-grid interval instead of emitting a zero-width quad.
			if (CrossingU == Previous.U)
			{
				Knots.Last().CoarseZ = Knots.Last().FineZ = CrossingZ;
			}
			else if (CrossingU == U)
			{
				Current.CoarseZ = Current.FineZ = CrossingZ;
			}
			else
			{
				Knots.Add({CrossingU, CrossingZ, CrossingZ});
			}
		}
		Knots.Add(Current);
		if (Knots.Num() > MaxKnots)
		{
			return false;
		}
		Previous = Current;
		PreviousCoarse = CoarseZ;
		PreviousFine = FineZ;
	}
	return true;
}

FVoxelLandM7NormalFrame FrameAt(
	double U, int32 Upper, const TArray<FKnot>& Knots, const FVoxelLandM7NormalFrameBatch& Frames)
{
	if (U == Knots[Upper].U)
	{
		return Frames.Frames[Upper];
	}
	const double T = (U - Knots[Upper - 1].U) / (Knots[Upper].U - Knots[Upper - 1].U);
	return FVoxelLandM7NormalFrames::BlendFromInterior(
		Frames.Frames[Upper - 1], Frames.Frames[Upper], uint16(FMath::RoundToInt(T * MAX_uint16)));
}

double RimZ(const FKnot& Knot, bool bCoarse)
{
	return bCoarse ? Knot.CoarseZ : Knot.FineZ;
}

double RimHeightAt(double U, int32 Upper, const TArray<FKnot>& Knots, bool bCoarse)
{
	if (U == Knots[Upper].U)
	{
		return RimZ(Knots[Upper], bCoarse);
	}
	const FKnot& A = Knots[Upper - 1];
	const FKnot& B = Knots[Upper];
	return FMath::Lerp(RimZ(A, bCoarse), RimZ(B, bCoarse), (U - A.U) / (B.U - A.U));
}

int32 FindKnot(const TArray<FKnot>& Knots, double U)
{
	return FindLower(Knots, U, [](const FKnot& Knot) { return Knot.U; });
}

void ApplyFrame(FVoxelLandMeshVertex& Vertex, const FVoxelLandM7NormalFrame& Frame)
{
	Vertex.Normal = Frame.Normal;
	Vertex.Tangent = Frame.Tangent;
}

bool EmitRims(FParticipant& Work, const FCut& Cut, const TArray<FKnot>& Knots,
	const FVoxelLandM7NormalFrameBatch& Frames, bool bCoarse, TFunctionRef<bool()> IsCancelled)
{
	for (const FPolygon& Polygon : Work.RimPolygons)
	{
		if (IsCancelled())
		{
			return false;
		}
		FPolygon Split;
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			FPoint A = Polygon[Index];
			const FPoint& B = Polygon[(Index + 1) % Polygon.Num()];
			if (A.P.X != 0.0)
			{
				AddDistinct(Split, A);
				continue;
			}
			const int32 Start = FindKnot(Knots, A.P.Y);
			const double OriginalZ = A.P.Z;
			A.P.Z = RimHeightAt(A.P.Y, Start, Knots, bCoarse);
			// Rounded crossings can shift the input line by at most a few grid
			// units. Other heights at this face parameter are an ambiguous rim.
			if (FMath::Abs(OriginalZ - A.P.Z) > 4.0 * Cut.Grid)
			{
				return false;
			}
			ApplyFrame(A.Vertex, FrameAt(A.P.Y, Start, Knots, Frames));
			AddDistinct(Split, A);
			if (B.P.X != 0.0 || A.P.Y == B.P.Y)
			{
				continue;
			}
			const int32 Step = A.P.Y < B.P.Y ? 1 : -1;
			int32 Knot = Start;
			if (Step > 0 && Knots[Start].U == A.P.Y)
			{
				++Knot;
			}
			if (Step < 0)
			{
				--Knot;
			}
			while (Knots.IsValidIndex(Knot) && (Step > 0 ? Knots[Knot].U < B.P.Y : Knots[Knot].U > B.P.Y))
			{
				const FKnot& Shared = Knots[Knot];
				FPoint Point = Interpolate(A, B, (Shared.U - A.P.Y) / (B.P.Y - A.P.Y));
				Point.P = {0.0, Shared.U, RimZ(Shared, bCoarse)};
				Point.Source = INDEX_NONE;
				ApplyFrame(Point.Vertex, Frames.Frames[Knot]);
				AddDistinct(Split, Point);
				Knot += Step;
			}
		}
		if (Split.Num() > 1 && Split[0].P == Split.Last().P)
		{
			Split.Pop(EAllowShrinking::No);
		}
		if (!EmitPolygon(Work, Cut, Split))
		{
			return false;
		}
	}
	for (int32 Index = 0; Index < Work.Mesh.Vertices.Num(); ++Index)
	{
		if ((Index % 256) == 0 && IsCancelled())
		{
			return false;
		}
		FVoxelLandMeshVertex& Vertex = Work.Mesh.Vertices[Index];
		const FVector P = Cut.ToPatch(Work.Origin + FVector(Vertex.Position));
		const double Distance = P.X * Work.Side;
		if (Distance <= 0.0 || Distance >= Work.Spacing || P.Y < 0.0 || P.Y > Cut.Width)
		{
			continue;
		}
		const int32 Knot = FindKnot(Knots, P.Y);
		if (FMath::Abs(P.Z - RimHeightAt(P.Y, Knot, Knots, bCoarse)) >= Work.Spacing)
		{
			continue;
		}
		const uint16 Weight = uint16(FMath::RoundToInt((1.0 - Distance / Work.Spacing) * MAX_uint16));
		const FVoxelLandM7NormalFrame Interior{Vertex.Normal, Vertex.Tangent};
		ApplyFrame(
			Vertex, FVoxelLandM7NormalFrames::BlendFromInterior(Interior, FrameAt(P.Y, Knot, Knots, Frames), Weight));
	}
	return true;
}

void AddCurtainVertex(
	FVoxelLandMeshData& Mesh, const FCut& Cut, const FVector& Origin, const FVector& Point, const FVector3f& Normal)
{
	FVoxelLandMeshVertex Vertex;
	Vertex.Position = FVector3f(Cut.ToLogical(Point) - Origin);
	Vertex.Normal = Normal;
	Vertex.Tangent = FVoxelLandM7NormalFrames::TangentForNormal(Normal);
	Vertex.UV = FVector2f(float(Point.Y / 100.0), float(Point.Z / 100.0));
	Mesh.Bounds += Vertex.Position;
	Mesh.Vertices.Add(Vertex);
}

bool EmitCurtain(const FCut& Cut, const FVector& CoarseOrigin, const TArray<FKnot>& Knots,
	TFunctionRef<bool()> IsCancelled, FVoxelLandM7SeamPatch& Patch)
{
	for (int32 Index = 1; Index < Knots.Num(); ++Index)
	{
		if (IsCancelled())
		{
			return false;
		}
		const FKnot& A = Knots[Index - 1];
		const FKnot& B = Knots[Index];
		const double GapA = A.CoarseZ - A.FineZ;
		const double GapB = B.CoarseZ - B.FineZ;
		if (GapA == 0.0 && GapB == 0.0)
		{
			continue;
		}
		const double Depth = FMath::Max(FMath::Abs(GapA), FMath::Abs(GapB)) + Cut.Clearance;
		if (Depth > Cut.DepthCap || (GapA < 0.0 && GapB > 0.0) || (GapA > 0.0 && GapB < 0.0))
		{
			return false;
		}
		Patch.MaxDepthCm = FMath::Max(Patch.MaxDepthCm, Depth);
		const FVector TopA(0.0, A.U, FMath::Max(A.CoarseZ, A.FineZ));
		const FVector TopB(0.0, B.U, FMath::Max(B.CoarseZ, B.FineZ));
		const FVector BottomA(0.0, A.U, FMath::Min(A.CoarseZ, A.FineZ) - Cut.Clearance);
		const FVector BottomB(0.0, B.U, FMath::Min(B.CoarseZ, B.FineZ) - Cut.Clearance);
		const int32 Outward = GapA + GapB > 0.0 ? -Cut.CoarseSide : Cut.CoarseSide;
		FVector3f Normal = FVector3f::ZeroVector;
		Normal[Cut.Axis] = float(Outward);
		const uint32 First = uint32(Patch.Curtain.Vertices.Num());
		for (const FVector& Point : {TopA, TopB, BottomB, BottomA})
		{
			AddCurtainVertex(Patch.Curtain, Cut, CoarseOrigin, Point, Normal);
		}
		const bool bFlip = (Cut.Axis == 0 ? -1 : 1) != Outward;
		const uint32 AId = First + 1;
		const uint32 BId = First + 2;
		const uint32 CId = First + 3;
		const uint32 Indices[] = {
			First, bFlip ? BId : AId, bFlip ? AId : BId, First, bFlip ? CId : BId, bFlip ? BId : CId};
		Patch.Curtain.Indices.Append(Indices, UE_ARRAY_COUNT(Indices));
		if (Index == 1)
		{
			Patch.Start = {Cut.ToLogical(TopA), Cut.ToLogical(BottomA), true};
		}
		if (Index == Knots.Num() - 1)
		{
			Patch.End = {Cut.ToLogical(TopB), Cut.ToLogical(BottomB), true};
		}
	}
	return true;
}

void SetParticipant(FParticipant& Work, const FVoxelLandM7SeamMesh& Source, const FCut& Cut, int32 Side, double Spacing,
	bool bMaxAcross, bool bMaxHeight)
{
	Work.Source = &Source;
	Work.Origin = VoxelLandMeshCoordinates::RenderOrigin(Source.Key.Coordinate, float(Spacing));
	Work.Side = Side;
	Work.Spacing = Spacing;
	Work.AllowedPositiveOwners =
		uint8((Side < 0 ? 1 << Cut.Axis : 0) | (bMaxAcross ? 1 << Cut.Across : 0) | (bMaxHeight ? 1 << 2 : 0));
}
}

FVoxelLandError VoxelLandM7Seams::BuildVerticalFace(const FVoxelLandM7SeamMesh& Coarse,
	TConstArrayView<FVoxelLandM7SeamMesh> Fine, const FVoxelLandM7VerticalFace& Face,
	TFunctionRef<FVoxelLandError(TConstArrayView<FVoxelLandM7NormalAnchor>, FVoxelLandM7NormalFrameBatch&)>
		EvaluateFrames,
	TFunctionRef<bool()> IsCancelled, FVoxelLandM7SeamPatch& OutPatch)
{
	if (Fine.Num() != 4)
	{
		return Reject(TEXT("M7 vertical seam requires four fine children"));
	}
	const FVoxelLandM7SeamMesh* Children[] = {&Fine[0], &Fine[1], &Fine[2], &Fine[3]};
	return BuildVerticalFace(Coarse, Children, Face, EvaluateFrames, IsCancelled, OutPatch);
}

FVoxelLandError VoxelLandM7Seams::BuildVerticalFace(const FVoxelLandM7SeamMesh& Coarse,
	TConstArrayView<const FVoxelLandM7SeamMesh*> Fine, const FVoxelLandM7VerticalFace& Face,
	TFunctionRef<FVoxelLandError(TConstArrayView<FVoxelLandM7NormalAnchor>, FVoxelLandM7NormalFrameBatch&)>
		EvaluateFrames,
	TFunctionRef<bool()> IsCancelled, FVoxelLandM7SeamPatch& OutPatch)
{
	if (IsCancelled())
	{
		return Cancel();
	}
	if (Face.Axis > 1 || Fine.Num() != 4 || Coarse.Key.LOD == 0 || Coarse.Key.LOD > 5 || Coarse.BaseVoxelSizeCm < 1
		|| Coarse.BaseVoxelSizeCm > 134217
		|| !ValidSource(Coarse, Coarse.Key.LOD, Coarse.BaseVoxelSizeCm, Coarse.bUnlimitedM4))
	{
		return Reject(TEXT("M7 requires a coarse vertical face and four adjacent-level children"));
	}
	FCut Cut;
	Cut.Axis = Face.Axis;
	Cut.Across = uint8(1 - Face.Axis);
	Cut.CoarseSide = Face.bPositiveFace ? -1 : 1;
	Cut.BaseSpacing = double(Coarse.BaseVoxelSizeCm);
	const double CoarseSpacing = double(1 << Coarse.Key.LOD) * Cut.BaseSpacing;
	const double FineSpacing = CoarseSpacing * 0.5;
	Cut.Width = 32.0 * CoarseSpacing;
	Cut.Grid = VoxelLandMeshCoordinates::Grid(float(CoarseSpacing));
	Cut.Clearance = FMath::Max(4.0, FMath::CeilToDouble(Cut.BaseSpacing * 0.0001 / Cut.Grid)) * Cut.Grid;
	Cut.DepthCap = Face.MaxDepthCm == 0.0 ? CoarseSpacing : Face.MaxDepthCm;
	if (!FMath::IsFinite(Cut.DepthCap) || Cut.DepthCap < Cut.Clearance || Cut.DepthCap > CoarseSpacing)
	{
		return Reject(TEXT("M7 curtain depth cap must fit within one coarse voxel"));
	}
	Cut.Origin = FVector(Coarse.Key.Coordinate) * Cut.Width;
	if (Face.bPositiveFace)
	{
		Cut.Origin[Cut.Axis] += Cut.Width;
	}

	FParticipant CoarseWork;
	SetParticipant(CoarseWork, Coarse, Cut, Cut.CoarseSide, CoarseSpacing, true, true);
	FParticipant FineWork[4];
	const FIntVector BaseChild = Coarse.Key.Coordinate * 2;
	uint8 ChildMask = 0;
	for (int32 Index = 0; Index < Fine.Num(); ++Index)
	{
		const FVoxelLandM7SeamMesh* Child = Fine[Index];
		if (!Child || !ValidSource(*Child, uint8(Coarse.Key.LOD - 1), Coarse.BaseVoxelSizeCm, Coarse.bUnlimitedM4))
		{
			return Reject(TEXT("Invalid fine terrain input for M7 vertical seam"));
		}
		const FIntVector Offset = Child->Key.Coordinate - BaseChild;
		const int32 Across = Offset[Cut.Across];
		if (Offset[Cut.Axis] != (Face.bPositiveFace ? 2 : -1) || Across < 0 || Across > 1 || Offset.Z < 0
			|| Offset.Z > 1)
		{
			return Reject(TEXT("Fine terrain child does not border the specified coarse face"));
		}
		const uint8 Bit = uint8(1 << (Across + 2 * Offset.Z));
		if ((ChildMask & Bit) != 0)
		{
			return Reject(TEXT("Duplicate fine terrain child in M7 vertical seam"));
		}
		ChildMask |= Bit;
		SetParticipant(FineWork[Index], *Child, Cut, -Cut.CoarseSide, FineSpacing, Across == 1, Offset.Z == 1);
	}

	TArray<FSegment> CoarseTrace;
	TArray<FSegment> FineTrace;
	if (const FVoxelLandError Error = PrepareParticipant(CoarseWork, Cut, CoarseTrace, IsCancelled))
	{
		return Error;
	}
	for (FParticipant& Work : FineWork)
	{
		if (const FVoxelLandError Error = PrepareParticipant(Work, Cut, FineTrace, IsCancelled))
		{
			return Error;
		}
	}
	FVoxelLandM7SeamPatch Patch;
	if (CoarseTrace.IsEmpty() && FineTrace.IsEmpty())
	{
		const EVoxelLandChunkClassification Classification = Coarse.Classification;
		if ((Classification != EVoxelLandChunkClassification::Empty
				&& Classification != EVoxelLandChunkClassification::Solid)
			|| !Coarse.Terrain.Indices.IsEmpty() || !Coarse.PositiveOwnerIndices.IsEmpty())
		{
			return Reject(TEXT("M7 cannot prove the absent vertical rim is empty"));
		}
		for (const FVoxelLandM7SeamMesh* Child : Fine)
		{
			if (Child->Classification != Classification || !Child->Terrain.Indices.IsEmpty()
				|| !Child->PositiveOwnerIndices.IsEmpty())
			{
				return Reject(TEXT("M7 cannot prove the absent fine vertical rim is empty"));
			}
		}
	}
	else
	{
		TArray<FKnot> Knots;
		if (!SortCompleteTrace(CoarseTrace, Cut) || !SortCompleteTrace(FineTrace, Cut)
			|| !MakeKnots(CoarseTrace, FineTrace, Cut, Knots))
		{
			return Reject(TEXT("M7 vertical rims are missing, stacked, disconnected or over budget"));
		}
		if (!EmitCurtain(Cut, CoarseWork.Origin, Knots, IsCancelled, Patch))
		{
			return IsCancelled() ? Cancel() : Reject(TEXT("M7 vertical curtain exceeds the measured depth cap"));
		}
		TArray<FVoxelLandM7NormalAnchor> Anchors;
		Anchors.Reserve(Knots.Num());
		for (const FKnot& Knot : Knots)
		{
			FVoxelLandM7NormalAnchor Anchor;
			if (!FVoxelLandM7NormalFrames::MakeAnchor(
					Cut.ToLogical({0.0, Knot.U, Knot.CoarseZ}), Coarse.BaseVoxelSizeCm, Anchor))
			{
				return Reject(TEXT("M7 vertical rim anchor is outside the normal source envelope"));
			}
			Anchors.Add(Anchor);
		}
		FVoxelLandM7NormalFrameBatch Frames;
		if (const FVoxelLandError Error = EvaluateFrames(Anchors, Frames))
		{
			return Error;
		}
		if (Frames.Frames.Num() != Knots.Num())
		{
			return Reject(TEXT("M7 vertical rim normal source returned an incomplete batch"));
		}
		for (const FVoxelLandM7NormalFrame& Frame : Frames.Frames)
		{
			if (Frame.Normal.ContainsNaN() || !Frame.Normal.IsNormalized() || Frame.Tangent.ContainsNaN()
				|| !Frame.Tangent.IsNormalized()
				|| FMath::Abs(FVector3f::DotProduct(Frame.Normal, Frame.Tangent)) > UE_KINDA_SMALL_NUMBER)
			{
				return Reject(TEXT("M7 vertical rim normal source returned an invalid frame"));
			}
		}
		Patch.SharedNormalFallbacks = Frames.FallbackCount;
		if (!EmitRims(CoarseWork, Cut, Knots, Frames, true, IsCancelled))
		{
			return IsCancelled() ? Cancel() : Reject(TEXT("Unsupported coarse rim split or shading"));
		}
		for (FParticipant& Work : FineWork)
		{
			if (!EmitRims(Work, Cut, Knots, Frames, false, IsCancelled))
			{
				return IsCancelled() ? Cancel() : Reject(TEXT("Unsupported fine rim split or shading"));
			}
		}
	}

	if (IsCancelled())
	{
		return Cancel();
	}
	Patch.CoarseTerrain = MoveTemp(CoarseWork.Mesh);
	for (FParticipant& Work : FineWork)
	{
		Patch.FineTerrain.Add(MoveTemp(Work.Mesh));
	}
	OutPatch = MoveTemp(Patch);
	return {};
}
