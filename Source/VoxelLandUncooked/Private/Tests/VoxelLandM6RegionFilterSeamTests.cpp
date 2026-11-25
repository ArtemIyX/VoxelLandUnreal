#if WITH_DEV_AUTOMATION_TESTS
#include "VoxelLandM6RegionFilterTestFixture.h"

#include "Async/Async.h"
#include "Data/Structs/VoxelLandM4UnlimitedConfig.h"
#include "Generation/VoxelLandM4CurveLut.h"
#include "Generation/VoxelLandMeshing.h"
#include "Misc/AutomationTest.h"

class FVoxelLandM6RegionSeamFixture final
{
public:
	static FString Run() { return RunMode(false); }
	static FString RunRedistance() { return RunMode(true); }

private:
	static FString RunMode(bool bRedistance);
	using FBase = FVoxelLandM6RegionFilterTestFixture;
	using FFixture = FBase::FFixture;
	using FChunkPtr = FBase::FChunkPtr;
	using FGeneratorPtr = FBase::FGeneratorPtr;
	using FGridPtr = FBase::FGridPtr;
	static constexpr int32 ChunkCount = 8;

	static FVoxelLandChunkKey Key(int32 Index);
	static bool SetLocalConfig(FVoxelLandM4UnlimitedConfig& Config);
	static int32 CountOwnedCrossings(const FVoxelLandChunkData& Chunk);
	static bool SharedSamplesAndCells(int32 A, int32 B, const FChunkPtr* Chunks,
		const FVoxelLandMeshData* Meshes, const FVoxelLandMeshingScratch* Scratch, int32& OutVertexCount);
	static FString CheckChunks(const FVoxelLandGenerator& Finite, const FVoxelLandGenerator& Local,
		const FVoxelLandGenerator& Baseline);
};

FVoxelLandChunkKey FVoxelLandM6RegionSeamFixture::Key(int32 Index)
{
	return {FIntVector((Index & 1) - 1, ((Index >> 1) & 1) - 1, ((Index >> 2) & 1) - 1)};
}

bool FVoxelLandM6RegionSeamFixture::SetLocalConfig(FVoxelLandM4UnlimitedConfig& Config)
{
	FVoxelLandGeneratorSettings ControlSettings;
	FVoxelLandProgramDraft Draft;
	Draft.FormatVersion = 2;
	Draft.RegisterCount = 1;
	Draft.Constants = {0.f};
	FVoxelLandInstruction Load, Store;
	Load.Op = EVoxelLandOpcode::LoadConstant;
	Store.Op = EVoxelLandOpcode::StoreDensity;
	Draft.Instructions = {Load, Store};
	FGeneratorPtr Control;
	if (FVoxelLandProgram::Validate(Draft, ControlSettings.Program) || FVoxelLandGenerator::Create(ControlSettings, Control))
	{
		return false;
	}
	TArray<int32> Heights;
	Heights.Init(0, FVoxelLandM4CurveLut::SampleCount);
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
	FString Error;
	if (!FVoxelLandM4CurveLut::Create(Heights, 0, 0, Curve, Error))
	{
		return false;
	}
	Config.ContinentalGenerator = Control;
	Config.MountainGenerator = Control;
	Config.PeaksGenerator = Control;
	Config.ContinentalCurve = Curve;
	Config.MountainCurve = Curve;
	Config.PeaksCurve = Curve;
	Config.BaseQ8 = -8000 * 256;
	Config.bNormalizeSlope = true;
	Config.SmoothOverride.Emplace();
	Config.SmoothOverride->bEnabled = true;
	Config.SmoothOverride->Strength = 0.5f;
	return true;
}

int32 FVoxelLandM6RegionSeamFixture::CountOwnedCrossings(const FVoxelLandChunkData& Chunk)
{
	const FIntVector Edges[] = {FIntVector(1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, 0, 1)};
	int32 Crossings = 0;
	for (int32 Z = 0; Z < FFixture::Cells; ++Z)
	{
		for (int32 Y = 0; Y < FFixture::Cells; ++Y)
		{
			for (int32 X = 0; X < FFixture::Cells; ++X)
			{
				const FIntVector Start(X, Y, Z);
				const bool bSolid = Chunk.Density[FFixture::SampleIndex(Start, FFixture::Samples)] < 0;
				for (const FIntVector& Step : Edges)
				{
					const bool bNeighborSolid = Chunk.Density[FFixture::SampleIndex(Start + Step, FFixture::Samples)] < 0;
					Crossings += int32(bSolid != bNeighborSolid);
				}
			}
		}
	}
	return Crossings;
}

bool FVoxelLandM6RegionSeamFixture::SharedSamplesAndCells(int32 A, int32 B, const FChunkPtr* Chunks,
	const FVoxelLandMeshData* Meshes, const FVoxelLandMeshingScratch* Scratch, int32& OutVertexCount)
{
	const FIntVector BaseA = Key(A).Coordinate * FFixture::Cells;
	const FIntVector BaseB = Key(B).Coordinate * FFixture::Cells;
	const FIntVector Min = BaseA.ComponentMax(BaseB) - FIntVector(1);
	const FIntVector Max = BaseA.ComponentMin(BaseB) + FIntVector(FFixture::Cells + 1);
	const FVector OriginA = VoxelLandMeshCoordinates::RenderOrigin(Key(A).Coordinate, FFixture::VoxelSizeCm);
	const FVector OriginB = VoxelLandMeshCoordinates::RenderOrigin(Key(B).Coordinate, FFixture::VoxelSizeCm);
	constexpr int32 CellAxis = FFixture::Samples - 1;

	OutVertexCount = 0;
	for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
	{
		for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
		{
			for (int32 X = Min.X; X <= Max.X; ++X)
			{
				const FIntVector Global(X, Y, Z);
				const FIntVector LA = Global - BaseA;
				const FIntVector LB = Global - BaseB;
				if (Chunks[A]->Density[FFixture::SampleIndex(LA, FFixture::Samples)]
					!= Chunks[B]->Density[FFixture::SampleIndex(LB, FFixture::Samples)])
				{
					return false;
				}
				if (X == Max.X || Y == Max.Y || Z == Max.Z)
				{
					continue;
				}
				const int32 VA = Scratch[A].CellVertices[FFixture::SampleIndex(LA, CellAxis)];
				const int32 VB = Scratch[B].CellVertices[FFixture::SampleIndex(LB, CellAxis)];
				if ((VA == INDEX_NONE) != (VB == INDEX_NONE))
				{
					return false;
				}
				if (VA != INDEX_NONE)
				{
					if (OriginA + FVector(Meshes[A].Vertices[VA].Position)
						!= OriginB + FVector(Meshes[B].Vertices[VB].Position))
					{
						return false;
					}
					++OutVertexCount;
				}
			}
		}
	}
	return true;
}

FString FVoxelLandM6RegionSeamFixture::CheckChunks(const FVoxelLandGenerator& Finite,
	const FVoxelLandGenerator& Local, const FVoxelLandGenerator& Baseline)
{
	FChunkPtr Chunks[ChunkCount];
	FVoxelLandMeshData Meshes[ChunkCount];
	FVoxelLandMeshingScratch Scratch[ChunkCount];
	TUniquePtr<FVoxelLandWorkerContext> FiniteWorker, LocalWorker, BaselineWorker;
	if (Finite.CreateWorker(FiniteWorker) || Local.CreateWorker(LocalWorker) || Baseline.CreateWorker(BaselineWorker))
	{
		return TEXT("Filtered seam workers could not be created");
	}
	bool bChanged = false;
	for (int32 Index = 0; Index < ChunkCount; ++Index)
	{
		const FVoxelLandChunkKey Coord = Key(Index);
		FChunkPtr LocalChunk, BeforeFilters;
		if (Finite.GenerateChunk(Coord, *FiniteWorker, Chunks[Index]) || Local.GenerateChunk(Coord, *LocalWorker, LocalChunk)
			|| Baseline.GenerateChunk(Coord, *BaselineWorker, BeforeFilters)
			|| Chunks[Index]->Hash != LocalChunk->Hash || Chunks[Index]->Density != LocalChunk->Density)
		{
			return TEXT("Finite and erosion-disabled local filtered fields differ");
		}
		bChanged |= Chunks[Index]->Density != BeforeFilters->Density;
		if (VoxelLandMeshing::BuildSurfaceNets(*LocalChunk, FFixture::VoxelSizeCm, Scratch[Index], Meshes[Index],
				&FFixture::NeverCancel, true) != EVoxelLandMeshResult::Success
			|| Meshes[Index].Indices.Num() != 6 * CountOwnedCrossings(*LocalChunk))
		{
			return TEXT("CPU mesh owned-edge count differs in a filtered chunk");
		}
	}
	if (!bChanged)
	{
		return TEXT("No real chunk was changed by the stage under test");
	}

	int32 AxisCounts[3] = {};
	int32 CornerVertices = 0;
	for (int32 A = 0; A < ChunkCount; ++A)
	{
		for (int32 B = A + 1; B < ChunkCount; ++B)
		{
			int32 Vertices = 0;
			if (!SharedSamplesAndCells(A, B, Chunks, Meshes, Scratch, Vertices))
			{
				return TEXT("Filtered stored density or CPU cell vertex differs across a seam");
			}
			const int32 Mask = A ^ B;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				if (Mask == (1 << Axis))
				{
					AxisCounts[Axis] += Vertices;
				}
			}
			if (Mask == 7)
			{
				CornerVertices += Vertices;
			}
		}
	}
	if (AxisCounts[0] == 0 || AxisCounts[1] == 0 || AxisCounts[2] == 0 || CornerVertices == 0)
	{
		return TEXT("The mesh seam fixture did not exercise every axis and the common corner");
	}

	for (int32 Index = ChunkCount - 1; Index >= 0; --Index)
	{
		FChunkPtr Reverse;
		if (Local.GenerateChunk(Key(Index), *LocalWorker, Reverse) || Chunks[Index]->Hash != Reverse->Hash
			|| Chunks[Index]->Density != Reverse->Density)
		{
			return TEXT("Reversed requests changed filtered local density");
		}
	}
	return {};
}

FString FVoxelLandM6RegionSeamFixture::RunMode(bool bRedistance)
{
	FVoxelLandGeneratorSettings Settings;
	FVoxelLandM4UnlimitedConfig Config;
	FGridPtr Grid;
	if (!FFixture::CreateInputs(-8000, Settings, Grid) || !SetLocalConfig(Config))
	{
		return TEXT("Reference finite or fixed-lattice local setup failed");
	}
	FVoxelLandM6Stamp Sphere;
	Sphere.Id = FGuid(0x4d36, 3, 1, 1);
	Sphere.OriginCm = FInt64Vector(-434, -411, -390);
	Sphere.RadiusCm = 765;
	Settings.M6Stamps.Stamps = MakeArrayView(&Sphere, 1);
	FGeneratorPtr Baseline, Finite, Local;
	if (FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, true, {}, Config.SmoothOverride.GetValue(), {}, Baseline))
	{
		return TEXT("Reference stamped baseline setup failed");
	}

	const FVoxelLandM6FilterStage Stages[] = {
		FBase::Stage(1, EVoxelLandM6FilterKind::Smooth, FInt64Vector(-650), FInt64Vector(300), 18000),
		FBase::Stage(2, EVoxelLandM6FilterKind::Flatten, FInt64Vector(-750, -500, -550), FInt64Vector(450), 8192),
		FBase::Stage(3, EVoxelLandM6FilterKind::Smooth, FInt64Vector(-500, -600, -500), FInt64Vector(300), 10000)};
	Settings.M6Filters.Stages = MakeArrayView(Stages);
	if (bRedistance)
	{
		if (FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, true, {}, Config.SmoothOverride.GetValue(), {}, Baseline))
		{
			return TEXT("Pre-re-distance filtered baseline setup failed");
		}
		Settings.M6Redistance.bEnabled = true;
		Settings.M6Redistance.BandVoxels = 2;
	}
	if (FVoxelLandGenerator::CreateM4HeightField(Settings, Grid, true, {}, Config.SmoothOverride.GetValue(), {}, Finite)
		|| FVoxelLandGenerator::CreateM4Unlimited(Settings, Config, Local))
	{
		return TEXT("Ordinary M6 filtered generators could not be prepared");
	}
	return CheckChunks(*Finite, *Local, *Baseline);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM6RegionFilterSeamTest, "VoxelLand.M6.Filters.FiniteAndLocalSeams",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM6RegionFilterSeamTest::RunTest(const FString& Parameters)
{
	const FString Error = Async(EAsyncExecution::ThreadPool, &FVoxelLandM6RegionSeamFixture::Run).Get();
	return TestTrue(Error.IsEmpty() ? TEXT("Sequential region filters across finite and local chunk seams") : Error, Error.IsEmpty());
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM6RedistanceSeamTest, "VoxelLand.M6.Redistance.FilteredFiniteAndLocalSeams",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM6RedistanceSeamTest::RunTest(const FString& Parameters)
{
	const FString Error = Async(EAsyncExecution::ThreadPool, &FVoxelLandM6RegionSeamFixture::RunRedistance).Get();
	return TestTrue(Error.IsEmpty() ? TEXT("Re-distance across filtered finite and local chunk seams") : Error, Error.IsEmpty());
}
#endif
