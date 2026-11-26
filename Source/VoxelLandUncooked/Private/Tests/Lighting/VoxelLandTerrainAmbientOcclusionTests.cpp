#if WITH_DEV_AUTOMATION_TESTS

#include "../TerrainOverlay/Stamps/VoxelLandTerrainSphereTestFixture.h"

#include "Data/Meshing/VoxelLandMeshData.h"
#include "Data/Terrain/ChunkLocal/VoxelLandChunkLocalTerrainConfig.h"
#include "Generation/Meshing/Lighting/VoxelLandTerrainAmbientOcclusion.h"
#include "Generation/Meshing/VoxelLandMeshing.h"

#include "Async/Async.h"
#include "Misc/AutomationTest.h"

class FVoxelLandAmbientOcclusionTestFixture final
{
public:
	using FGeneratorPtr = TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>;
	using FGridPtr = TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>;
	using FChunkPtr = TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>;

	static constexpr int32 SpacingCm = 100;
	static constexpr int32 DensityUnitsPerVoxel = 1024;
	static constexpr int32 TerraceY = 1800;
	static constexpr uint8 Bright = FVoxelLandTerrainAmbientOcclusion::FullyVisible;

	static bool NeverCancel() { return false; }
	static FVoxelLandError Air(const FInt64Vector&, int16& OutDensity);
	static FVoxelLandError Plane(const FInt64Vector& Position, int16& OutDensity);
	static FVoxelLandError Step(const FInt64Vector& Position, int16& OutDensity);
	static FVoxelLandError Channel(const FInt64Vector& Position, int16& OutDensity);
	static FString VerifyHeightSeams();
	static FString VerifyInfiniteHeight();
	static FString VerifyFiniteDirect();

private:
	static FVoxelLandError CreateConstantProgram(float Value, FVoxelLandGeneratorSettings& OutSettings);
	static bool CreateTerrace(FGeneratorPtr& OutGenerator);
	static FString BuildTerrainMesh(const FVoxelLandGenerator& Generator, FVoxelLandWorkerContext& Worker, int32 ChunkX,
		FVoxelLandMeshData& OutMesh);
	static FString CompareTerrainSeam(
		const FVoxelLandMeshData& Left, const FVoxelLandMeshData& Right, int32 FirstChunkX);
};

FVoxelLandError FVoxelLandAmbientOcclusionTestFixture::Air(const FInt64Vector&, int16& OutDensity)
{
	OutDensity = DensityUnitsPerVoxel;
	return {};
}

FVoxelLandError FVoxelLandAmbientOcclusionTestFixture::Plane(const FInt64Vector& Position, int16& OutDensity)
{
	OutDensity = static_cast<int16>(Position.Z * DensityUnitsPerVoxel);
	return {};
}

FVoxelLandError FVoxelLandAmbientOcclusionTestFixture::Step(const FInt64Vector& Position, int16& OutDensity)
{
	const int64 Surface = Position.X >= 2 ? 4 : 0;
	OutDensity = static_cast<int16>((Position.Z - Surface) * DensityUnitsPerVoxel);
	return {};
}

FVoxelLandError FVoxelLandAmbientOcclusionTestFixture::Channel(const FInt64Vector& Position, int16& OutDensity)
{
	const int64 Surface = FMath::Abs(Position.X) >= 2 ? 4 : 0;
	OutDensity = static_cast<int16>((Position.Z - Surface) * DensityUnitsPerVoxel);
	return {};
}

bool FVoxelLandAmbientOcclusionTestFixture::CreateTerrace(FGeneratorPtr& OutGenerator)
{
	constexpr int32 MinX = -6800;
	constexpr int32 MinY = -600;
	constexpr int32 CellsX = 136;
	constexpr int32 CellsY = 44;
	constexpr int32 HeightUnitsPerCm = 256;
	constexpr int32 LowHeightCm = 25;
	constexpr int32 HighHeightCm = 425;

	FVoxelLandGeneratorSettings Settings;
	FGridPtr UnusedGrid;
	if (!FVoxelLandM6SphereTestFixture::CreateInputs(LowHeightCm, Settings, UnusedGrid))
	{
		return false;
	}
	TArray<int32> Heights;
	Heights.SetNumUninitialized((CellsX + 1) * (CellsY + 1));
	for (int32 Y = 0; Y <= CellsY; ++Y)
	{
		const int32 ElevationCm = MinY + Y * SpacingCm >= TerraceY ? HighHeightCm : LowHeightCm;
		for (int32 X = 0; X <= CellsX; ++X)
		{
			Heights[Y * (CellsX + 1) + X] = ElevationCm * HeightUnitsPerCm;
		}
	}
	FGridPtr Grid;
	return FVoxelLandM4HeightGrid::Create(MinX, MinY, CellsX, CellsY, SpacingCm, Heights, Grid)
		&& !FVoxelLandM6SphereTestFixture::CreateGenerator(Settings, Grid, false, OutGenerator);
}

FString FVoxelLandAmbientOcclusionTestFixture::BuildTerrainMesh(
	const FVoxelLandGenerator& Generator, FVoxelLandWorkerContext& Worker, int32 ChunkX, FVoxelLandMeshData& OutMesh)
{
	FVoxelLandChunkKey Key;
	Key.Coordinate.X = ChunkX;
	FChunkPtr Chunk;
	if (const FVoxelLandError Error = Generator.GenerateChunk(Key, Worker, Chunk))
	{
		return Error.Message;
	}
	FVoxelLandMeshingScratch Scratch;
	if (VoxelLandMeshing::BuildSurfaceNets(*Chunk, SpacingCm, Scratch, OutMesh, NeverCancel)
		!= EVoxelLandMeshResult::Success)
	{
		return TEXT("The terrace did not produce a Surface Nets mesh");
	}
	const FVector3d Origin = VoxelLandMeshCoordinates::RenderOrigin(Key.Coordinate, SpacingCm);
	return FVoxelLandTerrainAmbientOcclusion::Bake(OutMesh, Generator, Worker, Key, Origin, NeverCancel, Chunk.Get())
		.Message;
}

FString FVoxelLandAmbientOcclusionTestFixture::CompareTerrainSeam(
	const FVoxelLandMeshData& Left, const FVoxelLandMeshData& Right, int32 FirstChunkX)
{
	if (Left.Vertices.Num() != Left.AmbientVisibility.Num() || Right.Vertices.Num() != Right.AmbientVisibility.Num())
	{
		return TEXT("The terrain mesh did not receive per-vertex visibility");
	}
	const FVector3d LeftOrigin = VoxelLandMeshCoordinates::RenderOrigin(FIntVector(FirstChunkX, 0, 0), SpacingCm);
	const FVector3d RightOrigin = VoxelLandMeshCoordinates::RenderOrigin(FIntVector(FirstChunkX + 1, 0, 0), SpacingCm);
	TMap<FVector3d, uint8> RightVisibility;
	for (int32 Index = 0; Index < Right.Vertices.Num(); ++Index)
	{
		RightVisibility.Add(RightOrigin + FVector3d(Right.Vertices[Index].Position), Right.AmbientVisibility[Index]);
	}
	int32 Shared = 0;
	int32 SharedDarkened = 0;
	for (int32 Index = 0; Index < Left.Vertices.Num(); ++Index)
	{
		const FVector3d Position = LeftOrigin + FVector3d(Left.Vertices[Index].Position);
		if (const uint8* Corresponding = RightVisibility.Find(Position))
		{
			if (*Corresponding != Left.AmbientVisibility[Index])
			{
				return TEXT("Adjacent meshes disagreed on the ambient visibility of a shared vertex");
			}
			++Shared;
			SharedDarkened += *Corresponding < Bright ? 1 : 0;
		}
	}
	return Shared > 0 && SharedDarkened > 0 ? FString()
											: TEXT("The terrace did not exercise an occluded shared vertex");
}

FString FVoxelLandAmbientOcclusionTestFixture::VerifyHeightSeams()
{
	FGeneratorPtr Generator;
	if (!CreateTerrace(Generator))
	{
		return TEXT("The terrace generator could not be created");
	}
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	if (const FVoxelLandError Error = Generator->CreateWorker(Worker))
	{
		return Error.Message;
	}
	constexpr int32 LeftChunkXs[] = {-2, 0};
	for (const int32 ChunkX : LeftChunkXs)
	{
		FVoxelLandMeshData Left;
		FVoxelLandMeshData Right;
		FString Failure = BuildTerrainMesh(*Generator, *Worker, ChunkX + 1, Right);
		if (!Failure.IsEmpty())
		{
			return Failure;
		}
		Failure = BuildTerrainMesh(*Generator, *Worker, ChunkX, Left);
		if (!Failure.IsEmpty())
		{
			return Failure;
		}
		Failure = CompareTerrainSeam(Left, Right, ChunkX);
		if (!Failure.IsEmpty())
		{
			return Failure;
		}
	}
	return {};
}

FVoxelLandError FVoxelLandAmbientOcclusionTestFixture::CreateConstantProgram(
	float Value, FVoxelLandGeneratorSettings& OutSettings)
{
	FVoxelLandProgramDraft Draft;
	Draft.FormatVersion = 2;
	Draft.RegisterCount = 1;
	Draft.Constants = {Value};
	FVoxelLandInstruction Load;
	Load.Op = EVoxelLandOpcode::LoadConstant;
	FVoxelLandInstruction Store;
	Store.Op = EVoxelLandOpcode::StoreDensity;
	Draft.Instructions = {Load, Store};
	OutSettings.Profile.VoxelSizeCm = SpacingCm;
	return FVoxelLandProgram::Validate(Draft, OutSettings.Program);
}

FString FVoxelLandAmbientOcclusionTestFixture::VerifyInfiniteHeight()
{
	FVoxelLandGeneratorSettings Settings;
	if (const FVoxelLandError Error = CreateConstantProgram(0.f, Settings))
	{
		return Error.Message;
	}
	FGeneratorPtr Source;
	if (const FVoxelLandError Error = FVoxelLandGenerator::Create(Settings, Source))
	{
		return Error.Message;
	}
	FVoxelLandM4UnlimitedConfig Config;
	Config.AuthoredHeightGenerator = Source;
	FGeneratorPtr Generator;
	if (const FVoxelLandError Error = FVoxelLandGenerator::CreateM4Unlimited(Settings, Config, Generator))
	{
		return Error.Message;
	}
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	if (const FVoxelLandError Error = Generator->CreateWorker(Worker))
	{
		return Error.Message;
	}
	FVoxelLandMeshData Mesh;
	Mesh.Vertices.AddDefaulted();
	const FVoxelLandChunkKey CoarseKey{FIntVector::ZeroValue, 2};
	if (const FVoxelLandError Error = FVoxelLandTerrainAmbientOcclusion::Bake(
			Mesh, *Generator, *Worker, CoarseKey, FVector3d::ZeroVector, NeverCancel))
	{
		return Error.Message;
	}
	return Mesh.AmbientVisibility == TArray<uint8>{Bright}
			 ? FString()
			 : TEXT("The infinite height plane did not provide neutral completed-field visibility");
}

FString FVoxelLandAmbientOcclusionTestFixture::VerifyFiniteDirect()
{
	FVoxelLandGeneratorSettings Settings;
	if (const FVoxelLandError Error = CreateConstantProgram(-100.f, Settings))
	{
		return Error.Message;
	}
	Settings.DirectDensityBounds.Emplace();
	Settings.DirectDensityBounds->MinChunk = FIntVector::ZeroValue;
	Settings.DirectDensityBounds->MaxChunkExclusive = FIntVector(1, 1, 1);
	FGeneratorPtr Generator;
	if (const FVoxelLandError Error = FVoxelLandGenerator::Create(Settings, Generator))
	{
		return Error.Message;
	}
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	if (const FVoxelLandError Error = Generator->CreateWorker(Worker))
	{
		return Error.Message;
	}
	FVoxelLandMeshData Mesh;
	constexpr int32 WidthCm = FVoxelLandChunkData::Cells * SpacingCm;
	Mesh.Vertices.AddDefaulted_GetRef().Position = FVector3f(WidthCm / 2.f, WidthCm / 2.f, WidthCm);
	if (const FVoxelLandError Error = FVoxelLandTerrainAmbientOcclusion::Bake(
			Mesh, *Generator, *Worker, FVoxelLandChunkKey(), FVector3d::ZeroVector, NeverCancel))
	{
		return Error.Message;
	}
	return Mesh.AmbientVisibility == TArray<uint8>{Bright}
			 ? FString()
			 : TEXT("The exterior tap did not respect finite direct density clipping");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandAmbientVisibilityKernelTest, "VoxelLand.Lighting.AmbientVisibility.Kernel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandAmbientVisibilityKernelTest::RunTest(const FString& Parameters)
{
	using FAmbientOcclusionFixture = FVoxelLandAmbientOcclusionTestFixture;
	FVoxelLandMeshData Mesh;
	Mesh.Vertices.AddDefaulted_GetRef().Position = FVector3f::ZeroVector;
	Mesh.Vertices.AddDefaulted_GetRef().Position = FVector3f(-8.f, 0.f, 0.f);
	const FVector3d Origin = FVector3d::ZeroVector;
	const TArray<uint8> BrightPair{FAmbientOcclusionFixture::Bright, FAmbientOcclusionFixture::Bright};
	TestFalse(TEXT("Air bake succeeds"), bool(FVoxelLandTerrainAmbientOcclusion::Bake(Mesh, 1.f, Origin,
											 FAmbientOcclusionFixture::Air, FAmbientOcclusionFixture::NeverCancel)));
	TestTrue(TEXT("Empty space stays neutral"), Mesh.AmbientVisibility == BrightPair);
	TestFalse(
		TEXT("Plane bake succeeds"), bool(FVoxelLandTerrainAmbientOcclusion::Bake(Mesh, 1.f, Origin,
										 FAmbientOcclusionFixture::Plane, FAmbientOcclusionFixture::NeverCancel)));
	TestTrue(TEXT("An exposed plane stays neutral"), Mesh.AmbientVisibility == BrightPair);

	TestFalse(
		TEXT("Terrace bake succeeds"), bool(FVoxelLandTerrainAmbientOcclusion::Bake(Mesh, 1.f, Origin,
										   FAmbientOcclusionFixture::Step, FAmbientOcclusionFixture::NeverCancel)));
	TestTrue(TEXT("The near step is darker than exposed terrain"),
		Mesh.AmbientVisibility == TArray<uint8>{191, FAmbientOcclusionFixture::Bright});
	Mesh.Vertices.Swap(0, 1);
	TestFalse(
		TEXT("Reversed bake succeeds"), bool(FVoxelLandTerrainAmbientOcclusion::Bake(Mesh, 1.f, Origin,
											FAmbientOcclusionFixture::Step, FAmbientOcclusionFixture::NeverCancel)));
	TestTrue(TEXT("Reversing anchors preserves bytes"),
		Mesh.AmbientVisibility == TArray<uint8>{FAmbientOcclusionFixture::Bright, 191});
	const TArray<uint8> Committed = Mesh.AmbientVisibility;
	int32 CancellationChecks = 0;
	const FVoxelLandError Cancelled = FVoxelLandTerrainAmbientOcclusion::Bake(Mesh, 1.f, Origin,
		FAmbientOcclusionFixture::Channel, [&CancellationChecks] { return ++CancellationChecks == 3; });
	TestEqual(TEXT("Cancellation before publication is reported"), Cancelled.Code, EVoxelLandErrorCode::Cancelled);
	TestTrue(TEXT("Cancellation preserves the previous payload"), Mesh.AmbientVisibility == Committed);

	Mesh.Vertices[0].Position.Z = 4.f; // The far anchor lies on the upper bank in the channel fixture.
	TestFalse(
		TEXT("Channel bake succeeds"), bool(FVoxelLandTerrainAmbientOcclusion::Bake(Mesh, 1.f, Origin,
										   FAmbientOcclusionFixture::Channel, FAmbientOcclusionFixture::NeverCancel)));
	TestTrue(TEXT("Half visibility rounds up to 128"),
		Mesh.AmbientVisibility == TArray<uint8>{FAmbientOcclusionFixture::Bright, 128});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandAmbientVisibilityFieldTest,
	"VoxelLand.Lighting.AmbientVisibility.CompletedField",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandAmbientVisibilityFieldTest::RunTest(const FString& Parameters)
{
	const FString HeightFailure =
		Async(EAsyncExecution::ThreadPool, &FVoxelLandAmbientOcclusionTestFixture::VerifyHeightSeams).Get();
	const FString InfiniteFailure =
		Async(EAsyncExecution::ThreadPool, &FVoxelLandAmbientOcclusionTestFixture::VerifyInfiniteHeight).Get();
	const FString DirectFailure =
		Async(EAsyncExecution::ThreadPool, &FVoxelLandAmbientOcclusionTestFixture::VerifyFiniteDirect).Get();
	if (!HeightFailure.IsEmpty())
	{
		AddError(HeightFailure);
	}
	if (!InfiniteFailure.IsEmpty())
	{
		AddError(InfiniteFailure);
	}
	if (!DirectFailure.IsEmpty())
	{
		AddError(DirectFailure);
	}
	return HeightFailure.IsEmpty() && InfiniteFailure.IsEmpty() && DirectFailure.IsEmpty();
}

#endif
