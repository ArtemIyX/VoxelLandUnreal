#if WITH_DEV_AUTOMATION_TESTS
#include "VoxelLandM7CheckpointTestFixture.h"
#include "VoxelLandM7PackedFrameTest.h"

#include "Async/Async.h"
#include "Generation/VoxelLandM7Seams.h"
#include "Generation/VoxelLandMeshing.h"
#include "Misc/AutomationTest.h"

namespace
{
using FFixture = FVoxelLandM7CheckpointTestFixture;
constexpr int32 Base = FFixture::VoxelSizeCm;
constexpr int32 Width = FFixture::Cells * 2 * Base;

enum class ETestTerrain : uint8 { Flat, Tilted, Valley };

struct FInputs
{
	FVoxelLandM7VerticalFace Face;
	FVoxelLandM7SeamMesh Coarse;
	TArray<FVoxelLandM7SeamMesh> Fine;
};

struct FRimVertex
{
	FVector P = FVector::ZeroVector;
	FVector3f Normal = FVector3f::ZeroVector;
	FVector3f Tangent = FVector3f::ZeroVector;
};

struct FRim
{
	TMap<double, FRimVertex> Vertices;
	TMap<double, double> Edges;
	uint8 FineRows = 0;
};

double FacePlane(const FInputs& Inputs)
{
	return double(Inputs.Coarse.Key.Coordinate[Inputs.Face.Axis] + int32(Inputs.Face.bPositiveFace)) * Width;
}

double AcrossMin(const FInputs& Inputs)
{
	return double(Inputs.Coarse.Key.Coordinate[1 - Inputs.Face.Axis]) * Width;
}

FVector MeshOrigin(const FVoxelLandChunkKey& Key)
{
	return VoxelLandMeshCoordinates::RenderOrigin(Key.Coordinate, float(Base * (1 << Key.LOD)));
}

int32 FixtureHeightQ8(const FInputs& Inputs, ETestTerrain Terrain, const FIntVector& Point)
{
	const int32 Bottom = Inputs.Coarse.Key.Coordinate.Z * Width;
	const int32 Height = Bottom + Width / 2 + (Terrain == ETestTerrain::Tilted ? 23 : 137);
	if (Terrain == ETestTerrain::Tilted)
	{
		const int32 Across = Point[1 - Inputs.Face.Axis] - int32(AcrossMin(Inputs)) - Width / 2;
		return Height * 256 + Across * 32;
	}
	const int32 NormalDistance = FMath::Abs(Point[Inputs.Face.Axis] - int32(FacePlane(Inputs)));
	return Height * 256 + (Terrain == ETestTerrain::Valley ? FMath::Min(NormalDistance, 2 * Base) * 32 : 0);
}

bool CreateFixture(const FInputs& Inputs, ETestTerrain Terrain, FFixture::FGeneratorPtr& Out)
{
	constexpr int32 GridOrigin = -100 * Base;
	constexpr int32 GridCells = 200;
	TArray<int32> Heights;
	Heights.Reserve((GridCells + 1) * (GridCells + 1));
	for (int32 Y = 0; Y <= GridCells; ++Y)
	{
		for (int32 X = 0; X <= GridCells; ++X)
		{
			const FIntVector P(GridOrigin + X * Base, GridOrigin + Y * Base, 0);
			Heights.Add(FixtureHeightQ8(Inputs, Terrain, P));
		}
	}
	FVoxelLandGeneratorSettings Settings;
	FFixture::FGridPtr Grid;
	return FFixture::CreateSettings(Settings)
		&& FVoxelLandM4HeightGrid::Create(GridOrigin, GridOrigin, GridCells, GridCells, Base, Heights, Grid)
		&& FFixture::CreateGenerator(Settings, Grid, false, false, Out);
}

bool CheckOrdinaryMesh(const FVoxelLandChunkData& Chunk, FVoxelLandM7SeamMesh& Out)
{
	FVoxelLandMeshingScratch Scratch;
	FVoxelLandMeshData Ordinary;
	if (VoxelLandMeshing::BuildM7SeamMesh(Chunk, Base, Scratch, Out, &FFixture::NeverCancel) != EVoxelLandMeshResult::Success
		|| VoxelLandMeshing::BuildSurfaceNets(Chunk, Base, Scratch, Ordinary, &FFixture::NeverCancel) != EVoxelLandMeshResult::Success
		|| Out.Terrain.Indices != Ordinary.Indices || Out.Terrain.Vertices.Num() != Ordinary.Vertices.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < Ordinary.Vertices.Num(); ++Index)
	{
		const FVoxelLandMeshVertex& A = Ordinary.Vertices[Index];
		const FVoxelLandMeshVertex& B = Out.Terrain.Vertices[Index];
		if (A.Position != B.Position || !FVoxelLandM7PackedFrameTest::Matches(A.Normal, A.Tangent, B.Normal, B.Tangent))
		{
			return false;
		}
	}
	return true;
}

bool PrepareInputs(const FVoxelLandGenerator& Generator, FVoxelLandWorkerContext& Worker, FInputs& Inputs)
{
	FFixture::FChunkPtr Chunk;
	const FVoxelLandChunkKey CoarseKey = Inputs.Coarse.Key;
	if (Generator.GenerateChunk(CoarseKey, Worker, Chunk) || !Chunk || !CheckOrdinaryMesh(*Chunk, Inputs.Coarse)
		|| Inputs.Coarse.PositiveOwnerIndices.IsEmpty())
	{
		return false;
	}
	for (int32 Child : {3, 0, 2, 1}) // Intentionally noncanonical submission order.
	{
		FIntVector Offset(0, 0, Child / 2);
		Offset[Inputs.Face.Axis] = Inputs.Face.bPositiveFace ? 2 : -1;
		Offset[1 - Inputs.Face.Axis] = Child % 2;
		const FVoxelLandChunkKey FineKey{CoarseKey.Coordinate * 2 + Offset, 0};
		Chunk.Reset();
		if (Generator.GenerateChunk(FineKey, Worker, Chunk) || !Chunk || !CheckOrdinaryMesh(*Chunk, Inputs.Fine.AddDefaulted_GetRef()))
		{
			return false;
		}
	}
	return true;
}

bool RecordRimVertex(const FInputs& Inputs, const FRimVertex& Vertex, FRim& Rim)
{
	const double U = Vertex.P[1 - Inputs.Face.Axis];
	if (const FRimVertex* Existing = Rim.Vertices.Find(U))
	{
		return Existing->P == Vertex.P
			&& FVoxelLandM7PackedFrameTest::Matches(Existing->Normal, Existing->Tangent, Vertex.Normal, Vertex.Tangent);
	}
	Rim.Vertices.Add(U, Vertex);
	return true;
}

bool CollectRim(const FInputs& Inputs, const FVoxelLandMeshData& Mesh, const FVoxelLandChunkKey& Key, FRim& Rim)
{
	const FVector Origin = MeshOrigin(Key);
	const double Plane = FacePlane(Inputs);
	const double Start = AcrossMin(Inputs);
	const int32 Across = 1 - Inputs.Face.Axis;
	bool bChildUsed = false;
	for (int32 Index = 0; Index < Mesh.Indices.Num(); Index += 3)
	{
		for (int32 Edge = 0; Edge < 3; ++Edge)
		{
			const FVoxelLandMeshVertex& A = Mesh.Vertices[Mesh.Indices[Index + Edge]];
			const FVoxelLandMeshVertex& B = Mesh.Vertices[Mesh.Indices[Index + (Edge + 1) % 3]];
			const FVector PA = Origin + FVector(A.Position), PB = Origin + FVector(B.Position);
			const double Min = FMath::Min(PA[Across], PB[Across]);
			const double Max = FMath::Max(PA[Across], PB[Across]);
			if (PA[Inputs.Face.Axis] != Plane || PB[Inputs.Face.Axis] != Plane
				|| Max <= Start || Min >= Start + Width)
			{
				continue;
			}
			if (Min < Start || Max > Start + Width || Min == Max || Rim.Edges.Contains(Min)
				|| !RecordRimVertex(Inputs, {PA, A.Normal, A.Tangent}, Rim)
				|| !RecordRimVertex(Inputs, {PB, B.Normal, B.Tangent}, Rim))
			{
				return false;
			}
			Rim.Edges.Add(Min, Max);
			bChildUsed = true;
		}
	}
	if (Key.LOD == 0 && bChildUsed)
	{
		Rim.FineRows |= uint8(1 << (Key.Coordinate.Z - 2 * Inputs.Coarse.Key.Coordinate.Z));
	}
	return true;
}

bool CheckPatch(const FInputs& Inputs, const FVoxelLandM7SeamPatch& Patch, ETestTerrain Terrain)
{
	FRim Coarse, Fine;
	if (Patch.FineTerrain.Num() != Inputs.Fine.Num()
		|| !CollectRim(Inputs, Patch.CoarseTerrain, Inputs.Coarse.Key, Coarse))
	{
		return false;
	}
	for (int32 Index = 0; Index < Inputs.Fine.Num(); ++Index)
	{
		if (!CollectRim(Inputs, Patch.FineTerrain[Index], Inputs.Fine[Index].Key, Fine))
		{
			return false;
		}
	}
	if (Coarse.Edges.IsEmpty() || Coarse.Edges.Num() != Fine.Edges.Num()
		|| Coarse.Vertices.Num() != Fine.Vertices.Num() || (Terrain == ETestTerrain::Tilted && Fine.FineRows != 3))
	{
		return false;
	}
	const double Start = AcrossMin(Inputs);
	const double Stop = Start + Width;
	double U = Start;
	for (int32 Index = 0; Index < Coarse.Edges.Num(); ++Index)
	{
		const double* CoarseEnd = Coarse.Edges.Find(U);
		const double* FineEnd = Fine.Edges.Find(U);
		if (!CoarseEnd || !FineEnd || *CoarseEnd != *FineEnd || *CoarseEnd <= U)
		{
			return false;
		}
		U = *CoarseEnd;
	}
	if (U != Stop)
	{
		return false;
	}
	for (const TPair<double, FRimVertex>& Vertex : Coarse.Vertices)
	{
		const FRimVertex* Other = Fine.Vertices.Find(Vertex.Key);
		if (!Other || !FVoxelLandM7PackedFrameTest::Matches(Vertex.Value.Normal, Vertex.Value.Tangent, Other->Normal, Other->Tangent))
		{
			return false;
		}
	}

	if (Terrain == ETestTerrain::Flat)
	{
		return Patch.Curtain.Indices.IsEmpty() && Patch.MaxDepthCm == 0.0 && !Patch.Start.bPresent && !Patch.End.bPresent;
	}
	if (Terrain == ETestTerrain::Valley && (Patch.Curtain.Indices.IsEmpty() || Patch.MaxDepthCm < 0.04 * Base
		|| Patch.MaxDepthCm > 0.09 * Base || !Patch.Start.bPresent || !Patch.End.bPresent))
	{
		return false;
	}
	const FVoxelLandMeshData& Wall = Patch.Curtain;
	if (Wall.Vertices.Num() % 4 != 0 || Wall.Indices.Num() != Wall.Vertices.Num() / 4 * 6)
	{
		return false;
	}
	const FVector Origin = MeshOrigin(Inputs.Coarse.Key);
	for (int32 Index = 0; Index < Wall.Vertices.Num(); Index += 4)
	{
		const FVector Top[] = {Origin + FVector(Wall.Vertices[Index].Position),
			Origin + FVector(Wall.Vertices[Index + 1].Position)};
		const FVector Bottom[] = {Origin + FVector(Wall.Vertices[Index + 3].Position),
			Origin + FVector(Wall.Vertices[Index + 2].Position)};
		for (int32 End = 0; End < 2; ++End)
		{
			const double At = Top[End][1 - Inputs.Face.Axis];
			const FRimVertex* A = Coarse.Vertices.Find(At);
			const FRimVertex* B = Fine.Vertices.Find(At);
			if (!A || !B || Top[End].X != Bottom[End].X || Top[End].Y != Bottom[End].Y
				|| Top[End][Inputs.Face.Axis] != FacePlane(Inputs) || Top[End].Z != FMath::Max(A->P.Z, B->P.Z)
				|| Bottom[End].Z >= FMath::Min(A->P.Z, B->P.Z) || Top[End].Z - Bottom[End].Z > Patch.MaxDepthCm)
			{
				return false;
			}
		}
		const FVector3f Normal = Wall.Vertices[Index].Normal;
		const int32 Winding = Index / 4 * 6;
		for (int32 Triangle = 0; Triangle < 6; Triangle += 3)
		{
			const FVector3f A = Wall.Vertices[Wall.Indices[Winding + Triangle]].Position;
			const FVector3f B = Wall.Vertices[Wall.Indices[Winding + Triangle + 1]].Position;
			const FVector3f C = Wall.Vertices[Wall.Indices[Winding + Triangle + 2]].Position;
			if (Normal.Z != 0.f || FMath::Abs(Normal[Inputs.Face.Axis]) != 1.f
				|| FVector3f::DotProduct(Normal, FVector3f::CrossProduct(B - A, C - A)) <= 0.f)
			{
				return false;
			}
		}
	}
	return (!Patch.Start.bPresent || (Patch.Start.Top[1 - Inputs.Face.Axis] == Start && Patch.Start.Top.Z > Patch.Start.Bottom.Z))
		&& (!Patch.End.bPresent || (Patch.End.Top[1 - Inputs.Face.Axis] == Stop && Patch.End.Top.Z > Patch.End.Bottom.Z));
}

bool CheckInterior(const FInputs& Inputs, const FVoxelLandMeshData& Mesh, const FVoxelLandM7SeamMesh& Source)
{
	const FVector Origin = MeshOrigin(Source.Key);
	const double Plane = FacePlane(Inputs);
	const double Band = Base * (1 << Source.Key.LOD);
	if (Mesh.Vertices.Num() < Source.Terrain.Vertices.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < Source.Terrain.Vertices.Num(); ++Index)
	{
		const FVoxelLandMeshVertex& A = Source.Terrain.Vertices[Index];
		const FVoxelLandMeshVertex& B = Mesh.Vertices[Index];
		if (A.Position != B.Position || (FMath::Abs(Origin[Inputs.Face.Axis] + A.Position[Inputs.Face.Axis] - Plane) >= Band
			&& !FVoxelLandM7PackedFrameTest::Matches(A.Normal, A.Tangent, B.Normal, B.Tangent)))
		{
			return false;
		}
	}
	return true;
}

bool ReplaceWithAnalytic(FVoxelLandM7SeamMesh& Mesh, bool bStacked)
{
	FVoxelLandChunkData Chunk;
	Chunk.Key = Mesh.Key;
	Chunk.Density.SetNumUninitialized(FVoxelLandChunkData::SampleCount);
	const int32 Spacing = Base * (1 << Mesh.Key.LOD);
	const int32 MinZ = Mesh.Key.Coordinate.Z * FFixture::Cells * Spacing;
	for (int32 Z = -1; Z <= 33; ++Z)
	{
		const int32 RelativeZ = MinZ + Z * Spacing + Width;
		const int32 Bottom = RelativeZ - Width / 4;
		const int32 UpperLayer = FMath::Max(Width / 2 - RelativeZ, RelativeZ - 3 * Width / 4);
		const int32 Density = bStacked ? FMath::Min(Bottom, UpperLayer) : Base;
		for (int32 Y = -1; Y <= 33; ++Y)
		{
			for (int32 X = -1; X <= 33; ++X)
			{
				Chunk.Density[FFixture::Index(FIntVector(X, Y, Z))] = int16(FMath::Clamp(Density * 1024 / Base, -32767, 32767));
			}
		}
	}
	FVoxelLandMeshingScratch Scratch;
	return VoxelLandMeshing::BuildM7SeamMesh(Chunk, Base, Scratch, Mesh, &FFixture::NeverCancel) == EVoxelLandMeshResult::Success;
}

FString RunCase(ETestTerrain Terrain, uint8 Axis, bool bPositive)
{
	FInputs Inputs;
	Inputs.Coarse.Key = {FIntVector(-1, -1, -1), 1};
	Inputs.Face = {Axis, bPositive};
	FFixture::FGeneratorPtr Generator;
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	if (!CreateFixture(Inputs, Terrain, Generator) || Generator->CreateWorker(Worker)
		|| !PrepareInputs(*Generator, *Worker, Inputs))
	{
		return TEXT("Ordinary finite generation, seam candidates, or unchanged legacy mesh differs");
	}
	bool bCancelled = false, bCancelAfterFrames = false;
	int32 FrameReads = 0;
	const auto Poll = [&bCancelled]() { return bCancelled; };
	const auto Frames = [&](TConstArrayView<FVoxelLandM7NormalAnchor> Anchors, FVoxelLandM7NormalFrameBatch& Batch)
	{
		++FrameReads;
		const FVoxelLandError Error = Generator->GenerateM7NormalFrames(Anchors, *Worker, Batch, Poll);
		bCancelled |= bCancelAfterFrames;
		return Error;
	};
	FVoxelLandM7SeamPatch Patch;
	const FVoxelLandError Error = VoxelLandM7Seams::BuildVerticalFace(Inputs.Coarse, Inputs.Fine, Inputs.Face, Frames, Poll, Patch);
	if (Error)
	{
		return FString::Printf(TEXT("Ordinary mixed vertical terrain failed to pair: %s"), *Error.Message);
	}
	if (FrameReads != 1 || !CheckPatch(Inputs, Patch, Terrain) || !CheckInterior(Inputs, Patch.CoarseTerrain, Inputs.Coarse))
	{
		return TEXT("Actual paired rim edges, packed shading, curtain geometry or interior frames differ");
	}
	for (int32 Index = 0; Index < Inputs.Fine.Num(); ++Index)
	{
		if (!CheckInterior(Inputs, Patch.FineTerrain[Index], Inputs.Fine[Index]))
		{
			return TEXT("Fine terrain changed outside the shading band");
		}
	}
	if (Terrain != ETestTerrain::Valley)
	{
		return {};
	}
	const int32 Prior = Patch.Curtain.Indices.Num();
	FVoxelLandM7VerticalFace Invalid = Inputs.Face;
	Invalid.Axis = 2;
	if (!VoxelLandM7Seams::BuildVerticalFace(Inputs.Coarse, Inputs.Fine, Invalid, Frames, Poll, Patch)
		|| Patch.Curtain.Indices.Num() != Prior)
	{
		return TEXT("Horizontal interface did not leave the previous patch intact");
	}
	Invalid = Inputs.Face;
	Invalid.MaxDepthCm = 0.02 * Base;
	if (!VoxelLandM7Seams::BuildVerticalFace(Inputs.Coarse, Inputs.Fine, Invalid, Frames, Poll, Patch)
		|| Patch.Curtain.Indices.Num() != Prior || FrameReads != 1)
	{
		return TEXT("Depth budget did not reject before normal reads");
	}
	bCancelAfterFrames = true;
	if (VoxelLandM7Seams::BuildVerticalFace(Inputs.Coarse, Inputs.Fine, Inputs.Face, Frames, Poll, Patch).Code
			!= EVoxelLandErrorCode::Cancelled || Patch.Curtain.Indices.Num() != Prior)
	{
		return TEXT("Cancellation after the normal read replaced the prior patch");
	}
	bCancelled = bCancelAfterFrames = false;
	if (!ReplaceWithAnalytic(Inputs.Fine[0], false)
		|| !ReplaceWithAnalytic(Inputs.Fine[1], false)
		|| !ReplaceWithAnalytic(Inputs.Fine[2], false)
		|| !ReplaceWithAnalytic(Inputs.Fine[3], false)
		|| !VoxelLandM7Seams::BuildVerticalFace(Inputs.Coarse, Inputs.Fine, Inputs.Face, Frames, Poll, Patch)
		|| Patch.Curtain.Indices.Num() != Prior)
	{
		return TEXT("Missing opposing rim did not leave the previous patch intact");
	}
	if (!ReplaceWithAnalytic(Inputs.Coarse, true))
	{
		return TEXT("Stacked-rim source did not mesh");
	}
	for (FVoxelLandM7SeamMesh& Fine : Inputs.Fine)
	{
		if (!ReplaceWithAnalytic(Fine, true))
		{
			return TEXT("Fine stacked-rim source did not mesh");
		}
	}
	if (!VoxelLandM7Seams::BuildVerticalFace(Inputs.Coarse, Inputs.Fine, Inputs.Face, Frames, Poll, Patch)
		|| Patch.Curtain.Indices.Num() != Prior)
	{
		return TEXT("Stacked rims did not leave the previous patch intact");
	}
	if (!ReplaceWithAnalytic(Inputs.Coarse, false))
	{
		return TEXT("Uniform source did not mesh");
	}
	for (FVoxelLandM7SeamMesh& Fine : Inputs.Fine)
	{
		if (!ReplaceWithAnalytic(Fine, false))
		{
			return TEXT("Fine uniform source did not mesh");
		}
	}
	const int32 PriorReads = FrameReads;
	if (VoxelLandM7Seams::BuildVerticalFace(Inputs.Coarse, Inputs.Fine, Inputs.Face, Frames, Poll, Patch)
		|| !Patch.Curtain.Indices.IsEmpty() || !Patch.CoarseTerrain.Indices.IsEmpty() || FrameReads != PriorReads)
	{
		return TEXT("Proven uniformly empty face did not resolve without a curtain or normal reads");
	}
	return {};
}

FString RunVerticalSeams()
{
	for (uint8 Axis : {uint8(0), uint8(1)})
	{
		for (bool bPositive : {false, true})
		{
			const FString Error = RunCase(ETestTerrain::Flat, Axis, bPositive);
			if (!Error.IsEmpty())
			{
				return Error;
			}
		}
	}
	const FString Tilted = RunCase(ETestTerrain::Tilted, 0, true);
	return Tilted.IsEmpty() ? RunCase(ETestTerrain::Valley, 1, false) : Tilted;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM7VerticalSeamTest, "VoxelLand.M7.Seams.VerticalTerrain",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM7VerticalSeamTest::RunTest(const FString& Parameters)
{
	const FString Error = Async(EAsyncExecution::Thread, &RunVerticalSeams).Get();
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}
	return Error.IsEmpty();
}
#endif
