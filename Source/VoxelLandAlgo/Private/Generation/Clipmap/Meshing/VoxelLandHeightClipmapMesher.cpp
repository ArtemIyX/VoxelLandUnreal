#include "Generation/Clipmap/Meshing/VoxelLandHeightClipmapMesher.h"

#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Data/Meshing/VoxelLandMeshData.h"
#include "Generation/Clipmap/Seams/VoxelLandSeamNormalFrames.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

class FVoxelLandHeightClipmapTile final
{
public:
	static constexpr uint8 AllEdges = 0xF;

	FVoxelLandHeightClipmapTile(const FVoxelLandChunkKey& Key, int64 InBaseSpacing, int64 InSpacing, uint8 InEdges);
	FVoxelLandError Build(
		const FVoxelLandGenerator& Generator, TFunctionRef<bool()> IsCancelled, FVoxelLandMeshData& Out);

private:
	static constexpr int32 Cells = FVoxelLandChunkData::Cells;
	static constexpr int32 Axis = Cells + 1;
	static constexpr int32 HaloAxis = Axis + 2;
	static constexpr int32 EdgeCount = 4;
	static constexpr int32 FrameSamples = 4;
	static constexpr double Q8PerCm = 256.0;
	static constexpr float CmPerMeter = 100.0f;
	static constexpr int32 RingEdges[EdgeCount] = {2, 1, 3, 0};

	static int32 GridVertex(int32 X, int32 Y) { return X + Axis * Y; }
	static int32 GridSample(int32 X, int32 Y) { return (X + 1) + HaloAxis * (Y + 1); }
	void QueueSamples();
	int32 QueueExactFrame(const FInt64Vector& World);
	void AddVertex(int32 HeightIndex, int32 FrameIndex);
	void AddCell(int32 X, int32 Y);
	bool HasEdge(int32 Edge) const { return (Edges & (1u << Edge)) != 0; }

	int64 BaseSpacing;
	int64 Spacing;
	FInt64Vector Origin;
	uint8 Edges;
	TArray<FInt64Vector> Positions;
	TArray<int32> Heights;
	TArray<int32> FrameIndices;
	int32 MidpointSamples[EdgeCount][Cells];
	int32 MidpointVertices[EdgeCount][Cells];
	FVoxelLandMeshData Mesh;
};

FVoxelLandHeightClipmapTile::FVoxelLandHeightClipmapTile(
	const FVoxelLandChunkKey& Key, int64 InBaseSpacing, int64 InSpacing, uint8 InEdges)
	: BaseSpacing(InBaseSpacing)
	, Spacing(InSpacing)
	, Origin(FInt64Vector(Key.Coordinate) * (Cells * Spacing))
	, Edges(InEdges)
{
}

int32 FVoxelLandHeightClipmapTile::QueueExactFrame(const FInt64Vector& World)
{
	const int32 Start = Positions.Num();
	Positions.Add(World - FInt64Vector(BaseSpacing, 0, 0));
	Positions.Add(World + FInt64Vector(BaseSpacing, 0, 0));
	Positions.Add(World - FInt64Vector(0, BaseSpacing, 0));
	Positions.Add(World + FInt64Vector(0, BaseSpacing, 0));
	return Start;
}

void FVoxelLandHeightClipmapTile::QueueSamples()
{
	Positions.Reserve(HaloAxis * HaloAxis + EdgeCount * Cells * (1 + 2 * FrameSamples));
	for (int32 Y = -1; Y <= Cells + 1; ++Y)
	{
		for (int32 X = -1; X <= Cells + 1; ++X)
		{
			Positions.Add(Origin + FInt64Vector(X * Spacing, Y * Spacing, 0));
		}
	}

	FrameIndices.Init(INDEX_NONE, Axis * Axis);
	if (Spacing == BaseSpacing)
	{
		return;
	}

	// The same base-spacing stencil is evaluated on both sides of every tile edge.
	for (int32 Y = 0; Y <= Cells; ++Y)
	{
		for (int32 X = 0; X <= Cells; ++X)
		{
			if (X == 0 || X == Cells || Y == 0 || Y == Cells)
			{
				const FInt64Vector World = Positions[GridSample(X, Y)];
				FrameIndices[GridVertex(X, Y)] = QueueExactFrame(World);
			}
		}
	}

	for (int32 Edge = 0; Edge < EdgeCount; ++Edge)
	{
		if (!HasEdge(Edge))
		{
			continue;
		}
		const int64 Across = (Edge & 1) != 0 ? Cells * Spacing : 0;
		for (int32 Index = 0; Index < Cells; ++Index)
		{
			const int64 Along = Index * Spacing + Spacing / 2;
			const FInt64Vector World =
				Origin + (Edge < 2 ? FInt64Vector(Across, Along, 0) : FInt64Vector(Along, Across, 0));
			MidpointSamples[Edge][Index] = Positions.Add(World);
			QueueExactFrame(World);
		}
	}
}

void FVoxelLandHeightClipmapTile::AddVertex(int32 HeightIndex, int32 FrameIndex)
{
	const FInt64Vector& World = Positions[HeightIndex];
	const FInt64Vector Relative = World - Origin;
	const int64 HeightQ8 = Heights[HeightIndex];
	double GradientX = 0.0;
	double GradientY = 0.0;
	if (FrameIndex != INDEX_NONE)
	{
		const double Denominator = 2.0 * BaseSpacing * Q8PerCm;
		GradientX = (int64(Heights[FrameIndex + 1]) - Heights[FrameIndex]) / Denominator;
		GradientY = (int64(Heights[FrameIndex + 3]) - Heights[FrameIndex + 2]) / Denominator;
	}
	else
	{
		const double Denominator = 2.0 * Spacing * Q8PerCm;
		GradientX = (int64(Heights[HeightIndex + 1]) - Heights[HeightIndex - 1]) / Denominator;
		GradientY = (int64(Heights[HeightIndex + HaloAxis]) - Heights[HeightIndex - HaloAxis]) / Denominator;
	}

	FVoxelLandMeshVertex& Vertex = Mesh.Vertices.AddDefaulted_GetRef();
	Vertex.Position = FVector3f(float(Relative.X), float(Relative.Y), float(double(HeightQ8) / Q8PerCm));
	Vertex.Normal = FVector3f(-float(GradientX), -float(GradientY), 1.0f).GetSafeNormal();
	Vertex.Tangent = FVoxelLandM7NormalFrames::TangentForNormal(Vertex.Normal);
	Vertex.UV = FVector2f(float(double(World.X) / CmPerMeter), float(double(World.Y) / CmPerMeter));
	Mesh.Bounds += Vertex.Position;
}

void FVoxelLandHeightClipmapTile::AddCell(int32 X, int32 Y)
{
	const int32 Corners[EdgeCount] = {
		GridVertex(X, Y), GridVertex(X + 1, Y), GridVertex(X + 1, Y + 1), GridVertex(X, Y + 1)};
	const bool bBoundary[EdgeCount] = {Y == 0, X == Cells - 1, Y == Cells - 1, X == 0};
	const int32 Along[EdgeCount] = {X, Y, X, Y};
	bool bSplit[EdgeCount];
	for (int32 Edge = 0; Edge < EdgeCount; ++Edge)
	{
		bSplit[Edge] = bBoundary[Edge] && HasEdge(RingEdges[Edge]);
	}

	// Start the fan at a corner away from split edges to avoid degenerate triangles.
	int32 Anchor = 0;
	while (bSplit[Anchor] || bSplit[(Anchor + EdgeCount - 1) % EdgeCount])
	{
		++Anchor;
		check(Anchor < EdgeCount);
	}
	uint32 Boundary[2 * EdgeCount];
	int32 Count = 0;
	for (int32 Offset = 0; Offset < EdgeCount; ++Offset)
	{
		const int32 Edge = (Anchor + Offset) % EdgeCount;
		Boundary[Count++] = uint32(Corners[Edge]);
		if (bSplit[Edge])
		{
			Boundary[Count++] = uint32(MidpointVertices[RingEdges[Edge]][Along[Edge]]);
		}
	}
	for (int32 Index = 1; Index + 1 < Count; ++Index)
	{
		Mesh.Indices.Add(Boundary[0]);
		Mesh.Indices.Add(Boundary[Index]);
		Mesh.Indices.Add(Boundary[Index + 1]);
	}
}

FVoxelLandError FVoxelLandHeightClipmapTile::Build(
	const FVoxelLandGenerator& Generator, TFunctionRef<bool()> IsCancelled, FVoxelLandMeshData& Out)
{
	QueueSamples();
	if (const FVoxelLandError Error = Generator.SampleHeightClipmap(Positions, Heights, IsCancelled))
	{
		return Error;
	}
	Mesh.Vertices.Reserve(Axis * Axis + EdgeCount * Cells);
	Mesh.Indices.Reserve((2 * Cells * Cells + EdgeCount * Cells) * 3);
	for (int32 Y = 0; Y < Axis; ++Y)
	{
		for (int32 X = 0; X < Axis; ++X)
		{
			AddVertex(GridSample(X, Y), FrameIndices[GridVertex(X, Y)]);
		}
	}
	for (int32 Edge = 0; Edge < EdgeCount; ++Edge)
	{
		if (!HasEdge(Edge))
		{
			continue;
		}
		for (int32 Index = 0; Index < Cells; ++Index)
		{
			MidpointVertices[Edge][Index] = Mesh.Vertices.Num();
			const int32 Sample = MidpointSamples[Edge][Index];
			AddVertex(Sample, Sample + 1);
		}
	}
	for (int32 Y = 0; Y < Cells; ++Y)
	{
		for (int32 X = 0; X < Cells; ++X)
		{
			AddCell(X, Y);
		}
	}
	if (IsCancelled())
	{
		return {EVoxelLandErrorCode::Cancelled, TEXT("Height clipmap meshing cancelled")};
	}
	Out = MoveTemp(Mesh);
	return {};
}

FVoxelLandError FVoxelLandHeightClipmapMesher::Build(const FVoxelLandChunkKey& Key, uint8 CoarseEdgeMask,
	const FVoxelLandGenerator& Generator, TFunctionRef<bool()> IsCancelled, FVoxelLandMeshData& OutMesh)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_BuildHeightClipmapMesh);
	const float BaseSpacing = Generator.GetProfile().VoxelSizeCm;
	float Spacing = 0.0f;
	if (Key.Coordinate.Z != 0 || (CoarseEdgeMask & ~FVoxelLandHeightClipmapTile::AllEdges) != 0
		|| (Key.LOD == 0 && CoarseEdgeMask != 0)
		|| !VoxelLandMeshCoordinates::IsMeshKeyInPrecisionRange(Key, BaseSpacing, true)
		|| !VoxelLandMeshCoordinates::TryGetLevelSpacing(BaseSpacing, Key.LOD, Spacing))
	{
		return {EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid stitched height tile key or edge mask")};
	}

	FVoxelLandHeightClipmapTile Tile(Key, int64(BaseSpacing), int64(Spacing), CoarseEdgeMask);
	return Tile.Build(Generator, IsCancelled, OutMesh);
}
