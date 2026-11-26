#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Data/Meshing/VoxelLandMeshData.h"
#include "Data/Terrain/ChunkLocal/VoxelLandChunkLocalTerrainConfig.h"
#include "Generation/Clipmap/Meshing/VoxelLandHeightClipmapMesher.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Misc/AutomationTest.h"

class FVoxelLandHeightClipmapSeamFixture final
{
public:
	static FString Verify();

private:
	static constexpr int32 BaseSpacing = 100;
	static constexpr int32 Cells = FVoxelLandChunkData::Cells;
	static constexpr uint8 AllEdges = 0xF;

	static bool NeverCancel() { return false; }
	static bool CancelImmediately() { return true; }
	static FString VerifyLevel(const FVoxelLandGenerator& Generator, uint8 Level);
	static FString VerifyEdge(const FVoxelLandGenerator& Generator, const FVoxelLandChunkKey& Parent,
		const FVoxelLandMeshData& Coarse, int32 Edge);
	static bool RecordEdge(const FVoxelLandMeshData& Mesh, FVector3f Origin, int32 Axis, float Plane, int32 Spacing,
		TMap<int32, FVoxelLandMeshVertex>& Out);
};

bool FVoxelLandHeightClipmapSeamFixture::RecordEdge(const FVoxelLandMeshData& Mesh, FVector3f Origin, int32 Axis,
	float Plane, int32 Spacing, TMap<int32, FVoxelLandMeshVertex>& Out)
{
	const int32 Along = 1 - Axis;
	for (int32 Index = 0; Index < Mesh.Indices.Num(); Index += 3)
	{
		const FVoxelLandMeshVertex* Vertices[] = {&Mesh.Vertices[Mesh.Indices[Index]],
			&Mesh.Vertices[Mesh.Indices[Index + 1]], &Mesh.Vertices[Mesh.Indices[Index + 2]]};
		if (FVector3f::CrossProduct(
				Vertices[1]->Position - Vertices[0]->Position, Vertices[2]->Position - Vertices[0]->Position)
				.Z
			<= 0.0f)
		{
			return false;
		}
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const FVoxelLandMeshVertex& First = *Vertices[Corner];
			const FVoxelLandMeshVertex& Second = *Vertices[(Corner + 1) % 3];
			if (First.Position[Axis] + Origin[Axis] != Plane || Second.Position[Axis] + Origin[Axis] != Plane)
			{
				continue;
			}
			if (FMath::Abs(First.Position[Along] - Second.Position[Along]) != Spacing)
			{
				return false;
			}
			for (const FVoxelLandMeshVertex* Vertex : {&First, &Second})
			{
				FVoxelLandMeshVertex Global = *Vertex;
				Global.Position += Origin;
				Out.Add(int32(Global.Position[Along]), Global);
			}
		}
	}
	return true;
}

FString FVoxelLandHeightClipmapSeamFixture::VerifyEdge(const FVoxelLandGenerator& Generator,
	const FVoxelLandChunkKey& Parent, const FVoxelLandMeshData& Coarse, int32 Edge)
{
	const int32 Axis = Edge / 2;
	const int32 Along = 1 - Axis;
	const bool bPositive = (Edge & 1) != 0;
	const int32 FineSpacing = BaseSpacing << (Parent.LOD - 1);
	const int32 FineWidth = Cells * FineSpacing;
	const FVector3f CoarseOrigin = FVector3f(Parent.Coordinate * (2 * FineWidth));
	const float Plane = CoarseOrigin[Axis] + (bPositive ? 2 * FineWidth : 0);
	TMap<int32, FVoxelLandMeshVertex> CoarseEdge, FineEdge;
	if (!RecordEdge(Coarse, CoarseOrigin, Axis, Plane, FineSpacing, CoarseEdge))
	{
		return TEXT("The coarse mesh has invalid winding or an unsplit boundary segment");
	}

	for (int32 Neighbor = 0; Neighbor < 2; ++Neighbor)
	{
		FVoxelLandChunkKey Key{Parent.Coordinate * 2, uint8(Parent.LOD - 1)};
		Key.Coordinate[Axis] += bPositive ? 2 : -1;
		Key.Coordinate[Along] += Neighbor;
		FVoxelLandMeshData Fine;
		if (const FVoxelLandError Error = FVoxelLandHeightClipmapMesher::Build(Key, 0, Generator, NeverCancel, Fine))
		{
			return Error.Message;
		}
		if (!RecordEdge(Fine, FVector3f(Key.Coordinate * FineWidth), Axis, Plane, FineSpacing, FineEdge))
		{
			return TEXT("A fine neighbor has invalid winding or boundary segments");
		}
	}
	if (CoarseEdge.Num() != 2 * Cells + 1 || FineEdge.Num() != CoarseEdge.Num())
	{
		return TEXT("The coarse and fine boundary vertex counts disagree");
	}
	for (const TPair<int32, FVoxelLandMeshVertex>& Pair : CoarseEdge)
	{
		const FVoxelLandMeshVertex& CoarseVertex = Pair.Value;
		const FVoxelLandMeshVertex* FineVertex = FineEdge.Find(Pair.Key);
		if (!FineVertex || CoarseVertex.Position != FineVertex->Position || CoarseVertex.Normal != FineVertex->Normal
			|| CoarseVertex.Tangent != FineVertex->Tangent || CoarseVertex.UV != FineVertex->UV)
		{
			return TEXT("A shared height, frame or UV differs across the LOD seam");
		}
	}
	return {};
}

FString FVoxelLandHeightClipmapSeamFixture::VerifyLevel(const FVoxelLandGenerator& Generator, uint8 Level)
{
	const FVoxelLandChunkKey Parent{FIntVector(-2, -1, 0), Level};
	FVoxelLandMeshData Coarse;
	if (const FVoxelLandError Error =
			FVoxelLandHeightClipmapMesher::Build(Parent, AllEdges, Generator, NeverCancel, Coarse))
	{
		return Error.Message;
	}
	for (int32 Edge = 0; Edge < 4; ++Edge)
	{
		if (const FString Error = VerifyEdge(Generator, Parent, Coarse, Edge); !Error.IsEmpty())
		{
			return FString::Printf(TEXT("LOD %u, edge %d: %s"), uint32(Level), Edge, *Error);
		}
	}

	const int32 VertexCount = Coarse.Vertices.Num();
	if (FVoxelLandHeightClipmapMesher::Build(Parent, AllEdges, Generator, CancelImmediately, Coarse).Code
			!= EVoxelLandErrorCode::Cancelled
		|| Coarse.Vertices.Num() != VertexCount)
	{
		return TEXT("Cancelled height mesh construction changed the output");
	}
	return {};
}

FString FVoxelLandHeightClipmapSeamFixture::Verify()
{
	FVoxelLandProgramDraft Draft;
	Draft.FormatVersion = 2;
	Draft.RegisterCount = 1;
	Draft.Constants = {0.25f};
	FVoxelLandInstruction Load;
	Load.Op = EVoxelLandOpcode::LoadConstant;
	FVoxelLandInstruction Store;
	Store.Op = EVoxelLandOpcode::StoreDensity;
	Draft.Instructions = {Load, Store};

	FVoxelLandGeneratorSettings Settings;
	Settings.Profile.VoxelSizeCm = BaseSpacing;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Source;
	if (FVoxelLandProgram::Validate(Draft, Settings.Program) || FVoxelLandGenerator::Create(Settings, Source))
	{
		return TEXT("Unable to create the authored height source");
	}

	FVoxelLandM4UnlimitedConfig Config;
	Config.AuthoredHeightGenerator = Source;
	Config.HeightDetail.bEnabled = true;
	Config.HeightDetail.AmplitudeQ8 = 400 * 256;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
	if (FVoxelLandGenerator::CreateM4Unlimited(Settings, Config, Generator) || !Generator->SupportsHeightClipmap())
	{
		return TEXT("Unable to create the detail-enabled infinite height generator");
	}
	for (uint8 Level = 1; Level <= 5; ++Level)
	{
		if (const FString Error = VerifyLevel(*Generator, Level); !Error.IsEmpty())
		{
			return Error;
		}
	}

	Config.SmoothOverride = FVoxelLandM4SmoothSettings{};
	Config.SmoothOverride->bEnabled = true;
	Config.SmoothOverride->Strength = 1.f;
	if (FVoxelLandGenerator::CreateM4Unlimited(Settings, Config, Generator) || Generator->SupportsHeightClipmap())
	{
		return TEXT("Volumetric density smoothing entered the planar height path");
	}
	return {};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandHeightClipmapSeamsTest, "VoxelLand.Clipmap.Height.Seams",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandHeightClipmapSeamsTest::RunTest(const FString& Parameters)
{
	const FString Failure = Async(EAsyncExecution::ThreadPool, &FVoxelLandHeightClipmapSeamFixture::Verify).Get();
	if (!Failure.IsEmpty())
	{
		AddError(Failure);
		return false;
	}
	return true;
}

#endif
