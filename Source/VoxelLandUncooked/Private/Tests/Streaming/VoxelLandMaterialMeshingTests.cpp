#if WITH_DEV_AUTOMATION_TESTS

#include "Generation/Meshing/VoxelLandMaterialMeshing.h"
#include "Data/Meshing/VoxelLandMaterialAppearance.h"
#include "Generation/Meshing/VoxelLandMeshing.h"
#include "Misc/AutomationTest.h"

class FVoxelLandMaterialMeshingFixture final
{
public:
	static constexpr EAutomationTestFlags Flags =
		EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	static FVoxelLandMeshVertex Vertex(float Height, const FVector3f& Normal = FVector3f::UpVector)
	{
		FVoxelLandMeshVertex Result;
		Result.Position = FVector3f(3.f, -5.f, Height);
		Result.Normal = Normal;
		Result.UV = FVector2f(0.25f, 0.5f);
		Result.Color = FColor(64, 128, 192);
		return Result;
	}

	static int32 Weight(const FVoxelLandMaterialBlend& Blend, uint8 Slot)
	{
		int32 Result = 0;
		for (int32 Layer = 0; Layer < FVoxelLandMaterialBlend::LayerCount; ++Layer)
		{
			if (Blend.Slots[Layer] == Slot)
			{
				Result += Blend.Weights[Layer];
			}
		}
		return Result;
	}

	static int32 WeightSum(const FVoxelLandMaterialBlend& Blend)
	{
		int32 Sum = 0;
		for (const uint8 Weight : Blend.Weights)
		{
			Sum += Weight;
		}
		return Sum;
	}

	bool operator()()
	{
		constexpr int32 BeforePublication = 4;
		return ++CancellationChecks >= BeforePublication;
	}

	static bool NeverCancel() { return false; }

	int32 CancellationChecks = 0;

	static bool BuildSlopedChunk(const FIntVector& Coordinate, float Spacing, FVoxelLandMeshData& Out)
	{
		FVoxelLandChunkData Chunk;
		Chunk.Key.Coordinate = Coordinate;
		Chunk.Density.SetNumUninitialized(FVoxelLandChunkData::SampleCount);
		constexpr int32 Axis = FVoxelLandChunkData::SamplesPerAxis;
		constexpr int32 Cells = FVoxelLandChunkData::Cells;
		for (int32 Z = 0; Z < Axis; ++Z)
		{
			for (int32 Y = 0; Y < Axis; ++Y)
			{
				const int32 GlobalZ = Coordinate.Z * Cells + Z - 1;
				const int32 GlobalY = Coordinate.Y * Cells + Y - 1;
				const int16 Density = static_cast<int16>((2 * GlobalZ - GlobalY - 1) * 128);
				for (int32 X = 0; X < Axis; ++X)
				{
					Chunk.Density[X + Axis * (Y + Axis * Z)] = Density;
				}
			}
		}

		FVoxelLandMeshingScratch Scratch;
		return VoxelLandMeshing::BuildSurfaceNets(Chunk, Spacing, Scratch, Out, NeverCancel)
			== EVoxelLandMeshResult::Success;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandMaterialRuleTest, "VoxelLand.Materials.RuleEvaluation", FVoxelLandMaterialMeshingFixture::Flags)

bool FVoxelLandMaterialRuleTest::RunTest(const FString& Parameters)
{
	using FFixture = FVoxelLandMaterialMeshingFixture;
	FVoxelLandMaterialAppearance Appearance;
	Appearance.SlotCount = 3;
	Appearance.Rules.Add({1, EVoxelLandMaterialRuleInput::ElevationCm, -1000.0, 100.0});
	Appearance.Rules.Add({2, EVoxelLandMaterialRuleInput::SlopeDegrees, 90.0, 0.0});

	FVoxelLandMeshData Mesh;
	Mesh.Vertices = {FFixture::Vertex(-50.f), FFixture::Vertex(0.f), FFixture::Vertex(50.f),
		FFixture::Vertex(-50.f, FVector3f::ForwardVector), FFixture::Vertex(0.f, FVector3f::ForwardVector),
		FFixture::Vertex(50.f, -FVector3f::UpVector)};
	Mesh.Indices = {0, 1, 2, 3, 4, 5};
	const FVector3d Origin(-3200.0, 0.0, -1000.0);
	const FVoxelLandMeshData Original = Mesh;
	if (!TestTrue(TEXT("rules pack"), FVoxelLandMaterialMeshing::Pack(Mesh, Appearance, Origin)))
	{
		return false;
	}

	TestEqual(TEXT("below blend is fallback"), FFixture::Weight(Mesh.MaterialBlend[0], 0), 255);
	TestEqual(TEXT("center residual goes to lower slot"), FFixture::Weight(Mesh.MaterialBlend[1], 0), 128);
	TestEqual(TEXT("center blends with slot one"), FFixture::Weight(Mesh.MaterialBlend[1], 1), 127);
	TestEqual(TEXT("above blend is elevation material"), FFixture::Weight(Mesh.MaterialBlend[2], 1), 255);
	for (int32 Index = 3; Index < 6; ++Index)
	{
		TestEqual(TEXT("later slope rule wins at threshold and underside"),
			FFixture::Weight(Mesh.MaterialBlend[Index], 2), 255);
	}
	TestEqual(TEXT("legacy UV survives"), Mesh.Vertices[0].UV, Original.Vertices[0].UV);
	TestEqual(TEXT("legacy vertex color survives"), Mesh.Vertices[0].Color, Original.Vertices[0].Color);

	FVoxelLandMeshData Overlapping = Original;
	Appearance.Rules[1] = {2, EVoxelLandMaterialRuleInput::ElevationCm, -1000.0, 100.0};
	if (!TestTrue(TEXT("overlapping rules pack"), FVoxelLandMaterialMeshing::Pack(Overlapping, Appearance, Origin)))
	{
		return false;
	}
	TestEqual(TEXT("equal minor weights retain the lower slot"), FFixture::Weight(Overlapping.MaterialBlend[1], 0), 85);
	TestEqual(TEXT("equal minor weights drop the higher slot"), FFixture::Weight(Overlapping.MaterialBlend[1], 1), 0);
	TestEqual(TEXT("dominant slot weight renormalizes"), FFixture::Weight(Overlapping.MaterialBlend[1], 2), 170);

	FVoxelLandMeshData Invalid = Original;
	Appearance.Rules[0].Slot = Appearance.SlotCount;
	TestFalse(TEXT("invalid slot cannot pack"), FVoxelLandMaterialMeshing::Pack(Invalid, Appearance, Origin));
	TestTrue(
		TEXT("invalid mesh is unmodified"), Invalid.MaterialBlend.IsEmpty() && Invalid.Indices == Original.Indices);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandMaterialTriangleTest, "VoxelLand.Materials.TrianglePacking", FVoxelLandMaterialMeshingFixture::Flags)

bool FVoxelLandMaterialTriangleTest::RunTest(const FString& Parameters)
{
	using FFixture = FVoxelLandMaterialMeshingFixture;
	FVoxelLandMaterialAppearance Appearance;
	Appearance.SlotCount = 8;
	for (uint8 Slot = 1; Slot < Appearance.SlotCount; ++Slot)
	{
		Appearance.Rules.Add({Slot, EVoxelLandMaterialRuleInput::ElevationCm, Slot * 10.0, 2.0});
	}
	FVoxelLandMeshData Mesh;
	Mesh.Vertices = {FFixture::Vertex(10.f), FFixture::Vertex(30.f), FFixture::Vertex(50.f), FFixture::Vertex(70.f)};
	Mesh.Vertices[0].Position = FVector3f(0.f, 0.f, 10.f);
	Mesh.Vertices[1].Position = FVector3f(1.f, 0.f, 30.f);
	Mesh.Vertices[2].Position = FVector3f(0.f, 1.f, 50.f);
	Mesh.Vertices[3].Position = FVector3f(1.f, -1.f, 70.f);
	Mesh.Indices = {0, 1, 2, 1, 0, 3};
	Mesh.Bounds = FBox3f(FVector3f(0.f, -1.f, 10.f), FVector3f(1.f, 1.f, 70.f));
	const FVoxelLandMeshData Original = Mesh;
	FVoxelLandMeshData Cancelled = Original;
	FFixture CancelBeforePublication;
	TestFalse(TEXT("packing can cancel after staging"),
		FVoxelLandMaterialMeshing::Pack(Cancelled, Appearance, FVector3d::ZeroVector, CancelBeforePublication));
	TestTrue(TEXT("cancellation leaves the original untouched"),
		Cancelled.MaterialBlend.IsEmpty() && Cancelled.Indices == Original.Indices
			&& Cancelled.Vertices.Num() == Original.Vertices.Num());
	FVoxelLandMeshData Rebuilt = Mesh;
	if (!TestTrue(
			TEXT("stress fixture packs"), FVoxelLandMaterialMeshing::Pack(Mesh, Appearance, FVector3d::ZeroVector)))
	{
		return false;
	}
	if (!TestTrue(TEXT("stress fixture rebuilds"),
			FVoxelLandMaterialMeshing::Pack(Rebuilt, Appearance, FVector3d::ZeroVector)))
	{
		return false;
	}
	TestTrue(TEXT("indices repeat deterministically"), Mesh.Indices == Rebuilt.Indices);
	constexpr int32 IndicesPerSplit = 7 * 3;
	if (!TestEqual(TEXT("only interiors are split into seven triangles"), Mesh.Indices.Num(), IndicesPerSplit * 2))
	{
		return false;
	}
	TestTrue(TEXT("encoded endpoints of original shared edge are reused"),
		Mesh.Indices[0] == Mesh.Indices[IndicesPerSplit + 1] && Mesh.Indices[1] == Mesh.Indices[IndicesPerSplit]);
	TestTrue(TEXT("same geometric bounds"),
		Mesh.Bounds.Min == Original.Bounds.Min && Mesh.Bounds.Max == Original.Bounds.Max);
	for (int32 Index = 0; Index < Original.Vertices.Num(); ++Index)
	{
		TestTrue(TEXT("original position, normal, tangent and legacy channels survive"),
			Mesh.Vertices[Index].Position == Original.Vertices[Index].Position
				&& Mesh.Vertices[Index].Normal == Original.Vertices[Index].Normal
				&& Mesh.Vertices[Index].Tangent == Original.Vertices[Index].Tangent
				&& Mesh.Vertices[Index].UV == Original.Vertices[Index].UV
				&& Mesh.Vertices[Index].Color == Original.Vertices[Index].Color);
	}

	double TwiceAreaXY[2] = {};
	for (int32 Start = 0; Start < Mesh.Indices.Num(); Start += 3)
	{
		const FVector3d A(Mesh.Vertices[Mesh.Indices[Start]].Position);
		const FVector3d B(Mesh.Vertices[Mesh.Indices[Start + 1]].Position);
		const FVector3d C(Mesh.Vertices[Mesh.Indices[Start + 2]].Position);
		const double TwiceTriangleArea = FVector3d::CrossProduct(B - A, C - A).Z;
		TestTrue(TEXT("new triangles keep source winding"), TwiceTriangleArea > 0.0);
		TwiceAreaXY[Start / IndicesPerSplit] += TwiceTriangleArea;
		const FVoxelLandMaterialBlend& First = Mesh.MaterialBlend[Mesh.Indices[Start]];
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const int32 Index = Mesh.Indices[Start + Corner];
			const FVoxelLandMaterialBlend& Blend = Mesh.MaterialBlend[Index];
			const FVoxelLandMaterialBlend& Repeat = Rebuilt.MaterialBlend[Index];
			TestTrue(TEXT("triangle interpolates the same slots at all corners"),
				FMemory::Memcmp(First.Slots, Blend.Slots, sizeof(First.Slots)) == 0);
			TestEqual(TEXT("quantized weights sum exactly"), FFixture::WeightSum(Blend), 255);
			TestTrue(
				TEXT("material bytes repeat deterministically"), FMemory::Memcmp(&Blend, &Repeat, sizeof(Blend)) == 0);
		}
	}
	constexpr double AreaTolerance = 1e-6;
	for (int32 Triangle = 0; Triangle < UE_ARRAY_COUNT(TwiceAreaXY); ++Triangle)
	{
		const int32 Start = Triangle * 3;
		const FVector3d A(Original.Vertices[Original.Indices[Start]].Position);
		const FVector3d B(Original.Vertices[Original.Indices[Start + 1]].Position);
		const FVector3d C(Original.Vertices[Original.Indices[Start + 2]].Position);
		const double ExpectedArea = FVector3d::CrossProduct(B - A, C - A).Z;
		TestEqual(TEXT("inset triangles preserve their source triangle's projected area"), TwiceAreaXY[Triangle],
			ExpectedArea, AreaTolerance);
	}
	const FVoxelLandMaterialBlend& First = Mesh.MaterialBlend[Mesh.Indices[0]];
	TestTrue(TEXT("shared edge keeps all four original slots"),
		First.Slots[0] == 0 && First.Slots[1] == 1 && First.Slots[2] == 2 && First.Slots[3] == 3);
	TestEqual(TEXT("first endpoint keeps its lower slot contribution"), FFixture::Weight(First, 0), 128);
	TestEqual(TEXT("first endpoint keeps its upper slot contribution"), FFixture::Weight(First, 1), 127);
	const FVoxelLandMaterialBlend& Second = Mesh.MaterialBlend[Mesh.Indices[1]];
	TestEqual(TEXT("second endpoint keeps its lower slot contribution"), FFixture::Weight(Second, 2), 128);
	TestEqual(TEXT("second endpoint keeps its upper slot contribution"), FFixture::Weight(Second, 3), 127);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandMaterialChunkSeamTest, "VoxelLand.Materials.ChunkSeam", FVoxelLandMaterialMeshingFixture::Flags)

bool FVoxelLandMaterialChunkSeamTest::RunTest(const FString& Parameters)
{
	using FFixture = FVoxelLandMaterialMeshingFixture;
	constexpr float Spacing = 100.f;
	FVoxelLandMaterialAppearance Appearance;
	Appearance.SlotCount = 2;
	Appearance.Rules.Add({1, EVoxelLandMaterialRuleInput::ElevationCm, 800.0, 1800.0});

	for (const int32 LeftX : {-3, 2})
	{
		const FIntVector LeftKey(LeftX, 0, 0);
		const FIntVector RightKey(LeftX + 1, 0, 0);
		FVoxelLandMeshData Left;
		FVoxelLandMeshData Right;
		if (!TestTrue(TEXT("build right geometry first"), FFixture::BuildSlopedChunk(RightKey, Spacing, Right))
			|| !TestTrue(TEXT("build left geometry second"), FFixture::BuildSlopedChunk(LeftKey, Spacing, Left)))
		{
			return false;
		}
		const FVector3d LeftOrigin = VoxelLandMeshCoordinates::RenderOrigin(LeftKey, Spacing);
		const FVector3d RightOrigin = VoxelLandMeshCoordinates::RenderOrigin(RightKey, Spacing);
		if (!TestTrue(TEXT("left material packs"), FVoxelLandMaterialMeshing::Pack(Left, Appearance, LeftOrigin))
			|| !TestTrue(TEXT("right material packs"), FVoxelLandMaterialMeshing::Pack(Right, Appearance, RightOrigin)))
		{
			return false;
		}

		TMap<FVector3d, int32> LeftWeights;
		for (const uint32 Index : Left.Indices)
		{
			const FVector3d World = LeftOrigin + FVector3d(Left.Vertices[Index].Position);
			LeftWeights.FindOrAdd(World, FFixture::Weight(Left.MaterialBlend[Index], 1));
		}
		int32 SharedBlended = 0;
		for (const uint32 Index : Right.Indices)
		{
			const FVector3d World = RightOrigin + FVector3d(Right.Vertices[Index].Position);
			if (const int32* Weight = LeftWeights.Find(World))
			{
				TestEqual(TEXT("same logical vertex agrees across chunk seam"),
					FFixture::Weight(Right.MaterialBlend[Index], 1), *Weight);
				SharedBlended += *Weight > 0 && *Weight < FVoxelLandMaterialBlend::WeightSum ? 1 : 0;
			}
		}
		TestTrue(TEXT("shared vertices exercise a blend at the seam"), SharedBlended > 0);
	}
	return true;
}

#endif
