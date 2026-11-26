#if WITH_DEV_AUTOMATION_TESTS

#include "Generation/Collision/VoxelLandCollisionField.h"

#include "../TerrainOverlay/Filters/VoxelLandTerrainRegionFilterTestFixture.h"

#include "Data/Terrain/ChunkLocal/VoxelLandChunkLocalTerrainConfig.h"

#include "Async/Async.h"
#include "Misc/AutomationTest.h"

#include <initializer_list>

class FVoxelLandCollisionFieldFixture final
{
public:
	using FGeneratorPtr = TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>;
	using FChunkPtr = FVoxelLandCollisionField::FChunkPtr;
	using FChunkMap = FVoxelLandCollisionField::FChunkMap;
	using EStatus = EVoxelLandCollisionFieldStatus;

	static constexpr int32 VoxelSizeCm = FVoxelLandM6SphereTestFixture::VoxelSizeCm;
	static constexpr int32 HeightCm = 25;
	static constexpr int32 WidthCm = FVoxelLandChunkData::Cells * VoxelSizeCm;

	static FChunkPtr MakeAffineChunk(FIntVector Coordinate)
	{
		const TSharedRef<FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk =
			MakeShared<FVoxelLandChunkData, ESPMode::ThreadSafe>();
		constexpr int32 AffineChunkCells = FVoxelLandChunkData::Cells;
		constexpr int32 AffineSamplesPerAxis = FVoxelLandChunkData::SamplesPerAxis;
		Chunk->Key.Coordinate = Coordinate;
		Chunk->Density.Reserve(FVoxelLandChunkData::SampleCount);
		for (int32 Z = -1; Z < AffineSamplesPerAxis - 1; ++Z)
		{
			for (int32 Y = -1; Y < AffineSamplesPerAxis - 1; ++Y)
			{
				for (int32 X = -1; X < AffineSamplesPerAxis - 1; ++X)
				{
					const FIntVector Global = Coordinate * AffineChunkCells + FIntVector(X, Y, Z);
					Chunk->Density.Add(static_cast<int16>(128 * Global.X - 256 * Global.Y + 512 * Global.Z - 256));
				}
			}
		}
		return Chunk;
	}

	static FString VerifyFiniteHeight()
	{
		FVoxelLandGeneratorSettings Settings;
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
		if (!FVoxelLandM6SphereTestFixture::CreateInputs(HeightCm, Settings, Grid))
		{
			return TEXT("Failed to prepare the finite Height fixture");
		}
		FGeneratorPtr Generator;
		if (const FVoxelLandError Error =
				FVoxelLandM6SphereTestFixture::CreateGenerator(Settings, Grid, false, Generator))
		{
			return Error.Message;
		}
		return VerifyHeightSeams(*Generator);
	}

	static FString VerifyInfiniteHeight()
	{
		FVoxelLandGeneratorSettings Settings;
		if (const FVoxelLandError Error = CreateConstantProgram(0.0f, Settings))
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
		Config.BaseQ8 = HeightCm * 256;
		FGeneratorPtr Generator;
		if (const FVoxelLandError Error = FVoxelLandGenerator::CreateM4Unlimited(Settings, Config, Generator))
		{
			return Error.Message;
		}
		return VerifyHeightSeams(*Generator);
	}

	static FString VerifyCompletedOverlay()
	{
		FVoxelLandGeneratorSettings Settings;
		TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
		FGeneratorPtr Baseline;
		if (!FVoxelLandM6SphereTestFixture::CreateInputs(HeightCm, Settings, Grid)
			|| FVoxelLandM6SphereTestFixture::CreateGenerator(Settings, Grid, false, Baseline))
		{
			return TEXT("Failed to create the untouched overlay fixture");
		}

		FVoxelLandM6Stamp Sphere;
		Sphere.Id = FGuid(0x4d36, 3, 1, 11);
		Sphere.OriginCm = FInt64Vector(0, 150, HeightCm);
		Sphere.RadiusCm = 425;
		Settings.M6Stamps.Stamps = MakeArrayView(&Sphere, 1);
		const FVoxelLandM6FilterStage Smooth = FVoxelLandM6RegionFilterTestFixture::Stage(
			11, EVoxelLandM6FilterKind::Smooth, FInt64Vector(-400, -100, -300), FInt64Vector(400, 400, 600), 18000);
		Settings.M6Filters.Stages = MakeArrayView(&Smooth, 1);
		Settings.M6Redistance.bEnabled = true;
		Settings.M6Redistance.BandVoxels = 2;
		FGeneratorPtr Completed;
		if (const FVoxelLandError Error =
				FVoxelLandM6SphereTestFixture::CreateGenerator(Settings, Grid, false, Completed))
		{
			return Error.Message;
		}
		const FVoxelLandChunkKey Left{FIntVector(-1, 0, 0), 0};
		const FVoxelLandChunkKey Right{};
		FChunkMap BaseChunks;
		FChunkMap CompletedChunks;
		if (const FString Failure = Generate(*Baseline, {Right}, BaseChunks); !Failure.IsEmpty())
		{
			return Failure;
		}
		if (const FString Failure = Generate(*Completed, {Left, Right}, CompletedChunks); !Failure.IsEmpty())
		{
			return Failure;
		}
		const FVoxelLandCollisionField Before(VoxelSizeCm, MoveTemp(BaseChunks));
		const FVoxelLandCollisionField After(VoxelSizeCm, MoveTemp(CompletedChunks));
		const FVector3d SeamPoint(0.0, 150.0, 225.0);
		double BaseDensity = 0.0;
		double CompletedDensity = 0.0;
		if (Before.Sample(SeamPoint, BaseDensity) != EStatus::Available || BaseDensity <= 0.0
			|| After.Sample(SeamPoint, CompletedDensity) != EStatus::Available || CompletedDensity >= 0.0)
		{
			return TEXT("Completed collision samples did not see solid terrain added by the processed overlay");
		}
		double LeftCell[2][2][2];
		double RightCell[2][2][2];
		if (After.ReadCell(FInt64Vector(-1, 1, 2), LeftCell) != EStatus::Available
			|| After.ReadCell(FInt64Vector(0, 1, 2), RightCell) != EStatus::Available)
		{
			return TEXT("Completed overlay seam cells are missing");
		}
		const double LeftFace = FVoxelLandCollisionField::EvaluateCell(LeftCell, FVector3d(1.0, 0.5, 0.25));
		const double RightFace = FVoxelLandCollisionField::EvaluateCell(RightCell, FVector3d(0.0, 0.5, 0.25));
		return LeftFace == CompletedDensity && RightFace == CompletedDensity
				 ? FString()
				 : TEXT("Adjacent collision cells disagree with the processed canonical seam sample");
	}

	static FString VerifyClippedDensity()
	{
		FVoxelLandGeneratorSettings Settings;
		if (const FVoxelLandError Error = CreateConstantProgram(-100.0f, Settings))
		{
			return Error.Message;
		}
		FVoxelLandDensityBounds& Bounds = Settings.DirectDensityBounds.Emplace();
		Bounds.MinChunk = FIntVector::ZeroValue;
		Bounds.MaxChunkExclusive = FIntVector(1);
		FGeneratorPtr Generator;
		if (const FVoxelLandError Error = FVoxelLandGenerator::Create(Settings, Generator))
		{
			return Error.Message;
		}
		FChunkMap Chunks;
		const FVoxelLandChunkKey Key{};
		if (const FString Failure = Generate(*Generator, {Key}, Chunks); !Failure.IsEmpty())
		{
			return Failure;
		}
		const FVoxelLandCollisionField Field(VoxelSizeCm, MoveTemp(Chunks), Bounds);
		for (const double EdgeX : {0.0, static_cast<double>(WidthCm)})
		{
			const double Outward = EdgeX == 0.0 ? -1.0 : 1.0;
			const FVector3d Edge(EdgeX, WidthCm / 2.0, WidthCm / 2.0);
			double Density = 1.0;
			if (Field.Sample(Edge, Density) != EStatus::Available || Density != 0.0
				|| Field.Sample(Edge + FVector3d(25.0 * Outward, 0.0, 0.0), Density) != EStatus::Available
				|| Density != 0.25
				|| Field.Sample(Edge - FVector3d(25.0 * Outward, 0.0, 0.0), Density) != EStatus::Available
				|| Density != -0.25)
			{
				return TEXT("Collision interpolation did not preserve the completed Direct Density box face");
			}
		}
		const FVoxelLandCollisionField Unloaded(VoxelSizeCm, {}, Bounds);
		double Density = 0.0;
		if (Unloaded.Sample(FVector3d(WidthCm, VoxelSizeCm, VoxelSizeCm), Density) != EStatus::Missing)
		{
			return TEXT("The boundary was treated as proven exterior without its clipped halo");
		}
		return Unloaded.Sample(FVector3d(WidthCm + 2.0 * VoxelSizeCm, 0.0, 0.0), Density) == EStatus::Available
					&& Density > 0.0
				 ? FString()
				 : TEXT("Proven exterior required unloaded chunk data");
	}

private:
	static FVoxelLandError CreateConstantProgram(float Value, FVoxelLandGeneratorSettings& Settings)
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
		Settings.Profile.VoxelSizeCm = VoxelSizeCm;
		return FVoxelLandProgram::Validate(Draft, Settings.Program);
	}

	static FString Generate(
		const FVoxelLandGenerator& Generator, std::initializer_list<FVoxelLandChunkKey> Keys, FChunkMap& Out)
	{
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		if (const FVoxelLandError Error = Generator.CreateWorker(Worker))
		{
			return Error.Message;
		}
		for (const FVoxelLandChunkKey& Key : Keys)
		{
			FChunkPtr Data;
			if (const FVoxelLandError Error = Generator.GenerateChunk(Key, *Worker, Data))
			{
				return Error.Message;
			}
			Out.Add(Key, MoveTemp(Data));
		}
		return {};
	}

	static FString VerifyHeightSeams(const FVoxelLandGenerator& Generator)
	{
		FChunkMap Chunks;
		const FVoxelLandChunkKey Left{FIntVector(-1, 0, 0), 0};
		const FVoxelLandChunkKey Center{FIntVector::ZeroValue, 0};
		const FVoxelLandChunkKey Right{FIntVector(1, 0, 0), 0};
		if (const FString Failure = Generate(Generator, {Left, Center, Right}, Chunks); !Failure.IsEmpty())
		{
			return Failure;
		}
		const FVoxelLandCollisionField Field(VoxelSizeCm, MoveTemp(Chunks));
		for (const double SeamX : {0.0, static_cast<double>(WidthCm)})
		{
			double LeftCell[2][2][2];
			double RightCell[2][2][2];
			const int64 Seam = static_cast<int64>(SeamX / VoxelSizeCm);
			if (Field.ReadCell(FInt64Vector(Seam - 1, 1, 0), LeftCell) != EStatus::Available
				|| Field.ReadCell(FInt64Vector(Seam, 1, 0), RightCell) != EStatus::Available)
			{
				return TEXT("Generated Height seam samples are unavailable");
			}
			const double LeftInterface = FVoxelLandCollisionField::EvaluateCell(LeftCell, FVector3d(1.0, 0.5, 0.75));
			const double RightInterface = FVoxelLandCollisionField::EvaluateCell(RightCell, FVector3d(0.0, 0.5, 0.75));
			if (LeftInterface != RightInterface || LeftInterface != 0.5)
			{
				return TEXT("The two completed chunks disagree at their shared Height seam");
			}
			double OnSurface = 1.0;
			FVector3d Gradient;
			if (Field.Sample(FVector3d(SeamX, 150.0, HeightCm), OnSurface, &Gradient) != EStatus::Available
				|| OnSurface != 0.0 || Gradient != FVector3d(0.0, 0.0, 1.0))
			{
				return TEXT("Generated height surface does not agree in value and normal at the seam");
			}
		}
		return {};
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandCollisionFieldScalarTest, "VoxelLand.Collision.Field.Scalar",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandCollisionFieldScalarTest::RunTest(const FString& Parameters)
{
	using FFieldFixture = FVoxelLandCollisionFieldFixture;
	using EStatus = FFieldFixture::EStatus;
	FFieldFixture::FChunkMap Chunks;
	const FVoxelLandChunkKey Left{FIntVector(-1, 0, 0), 0};
	const FVoxelLandChunkKey Right{};
	const FFieldFixture::FChunkPtr LeftChunk = FFieldFixture::MakeAffineChunk(Left.Coordinate);
	const FFieldFixture::FChunkPtr RightChunk = FFieldFixture::MakeAffineChunk(Right.Coordinate);
	Chunks.Add(Left, LeftChunk);
	Chunks.Add(Right, RightChunk);
	const FVoxelLandCollisionField Field(FFieldFixture::VoxelSizeCm, MoveTemp(Chunks));
	FFieldFixture::FChunkMap Reversed;
	Reversed.Add(Right, RightChunk);
	Reversed.Add(Left, LeftChunk);
	const FVoxelLandCollisionField ReversedField(FFieldFixture::VoxelSizeCm, MoveTemp(Reversed));

	double Density = 0.0;
	FVector3d Gradient;
	TestTrue(TEXT("Fractional negative query selects the floor cell"),
		Field.Sample(FVector3d(-50.0, 225.0, 150.0), Density, &Gradient) == EStatus::Available);
	TestEqual(TEXT("Trilinear sample preserves affine density"), Density, -0.125);
	TestEqual(TEXT("Gradient preserves field axes"), Gradient, FVector3d(0.125, -0.25, 0.5));

	FVoxelLandChunkKey Owner;
	TestTrue(TEXT("Negative cell has a canonical owner"),
		FVoxelLandCollisionField::TryGetCellOwner(FInt64Vector(-33, 0, 0), Owner));
	TestEqual(TEXT("Floor division maps a negative cell to the lower chunk"), Owner.Coordinate, FIntVector(-2, 0, 0));
	double Corners[2][2][2]{};
	TestTrue(TEXT("Cell immediately below the seam uses its lower owner halo"),
		Field.ReadCell(FInt64Vector(-1, 2, 1), Corners) == EStatus::Available);
	const double LeftDensity = FVoxelLandCollisionField::EvaluateCell(Corners, FVector3d(1.0, 0.25, 0.5));
	TestTrue(TEXT("Exact seam sample uses the canonical upper owner"),
		Field.Sample(FVector3d(0.0, 225.0, 150.0), Density) == EStatus::Available);
	TestEqual(TEXT("Overlapping halo and inner sample agree"), Density, LeftDensity);
	double ReversedDensity = 0.0;
	TestTrue(TEXT("Reversed capture order can sample the seam"),
		ReversedField.Sample(FVector3d(0.0, 225.0, 150.0), ReversedDensity) == EStatus::Available);
	TestEqual(TEXT("Capture order preserves seam density"), ReversedDensity, Density);

	FFieldFixture::FChunkMap NeighborOnly;
	NeighborOnly.Add(Right, FFieldFixture::MakeAffineChunk(Right.Coordinate));
	const FVoxelLandCollisionField MissingOwner(FFieldFixture::VoxelSizeCm, MoveTemp(NeighborOnly));
	FVoxelLandChunkKey MissingKey;
	TestTrue(TEXT("A neighboring halo cannot stand in for missing canonical ownership"),
		MissingOwner.ReadCell(FInt64Vector(-1, 2, 1), Corners, &MissingKey) == EStatus::Missing);
	TestEqual(TEXT("The missing result names the canonical chunk"), MissingKey.Coordinate, Left.Coordinate);
	TestTrue(TEXT("Out-of-range coordinates remain invalid"),
		Field.Sample(FVector3d(1.0e30, 0.0, 0.0), Density) == EStatus::Invalid);

	FFieldFixture::FChunkMap Malformed;
	Malformed.Add(Right, MakeShared<FVoxelLandChunkData, ESPMode::ThreadSafe>());
	const FVoxelLandCollisionField BrokenData(FFieldFixture::VoxelSizeCm, MoveTemp(Malformed));
	TestTrue(TEXT("A pinned chunk with insufficient samples is invalid"),
		BrokenData.ReadCell(FInt64Vector::ZeroValue, Corners) == EStatus::Invalid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandCollisionFieldCompletedTest, "VoxelLand.Collision.Field.Completed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandCollisionFieldCompletedTest::RunTest(const FString& Parameters)
{
	using FFieldFixture = FVoxelLandCollisionFieldFixture;
	const FString Failures[] = {
		Async(EAsyncExecution::ThreadPool, &FFieldFixture::VerifyFiniteHeight).Get(),
		Async(EAsyncExecution::ThreadPool, &FFieldFixture::VerifyInfiniteHeight).Get(),
		Async(EAsyncExecution::ThreadPool, &FFieldFixture::VerifyClippedDensity).Get(),
		Async(EAsyncExecution::ThreadPool, &FFieldFixture::VerifyCompletedOverlay).Get(),
	};
	for (const FString& Failure : Failures)
	{
		if (!Failure.IsEmpty())
		{
			AddError(Failure);
		}
	}
	return true;
}

#endif
