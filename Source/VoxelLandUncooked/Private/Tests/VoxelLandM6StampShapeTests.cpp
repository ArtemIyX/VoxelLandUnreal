#if WITH_DEV_AUTOMATION_TESTS
#include "VoxelLandM6SphereTestFixture.h"

#include "Async/Async.h"
#include "Generation/VoxelLandM6StampComposition.h"
#include "Misc/AutomationTest.h"

class FVoxelLandM6StampShapeFixture final
{
public:
	static FString Run();

private:
	using FFixture = FVoxelLandM6SphereTestFixture;
	using FGeneratorPtr = TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>;
	using FGridPtr = TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>;
	using FChunkPtr = TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>;

	static const FVoxelLandChunkKey Key;
	static FVoxelLandM6Stamp MakeBox(uint32 Id, const FInt64Vector& Center, const FIntVector& HalfExtent);
	static FVoxelLandM6Stamp MakeCapsule();
	static int16 Sample(const FVoxelLandChunkData& Chunk, const FIntVector& GlobalLattice);
	static bool Generate(FVoxelLandGeneratorSettings Settings, const FGridPtr& Grid,
		TConstArrayView<FVoxelLandM6Stamp> Stamps, FChunkPtr& Out, FGeneratorPtr& OutGenerator);
	static bool RejectInvalid(const FVoxelLandGeneratorSettings& Settings, const FGridPtr& Grid,
		const FVoxelLandM6Stamp& Box, const FVoxelLandM6Stamp& Capsule, const FGeneratorPtr& Prior);
	static bool CheckFoundation();
	static bool CheckSaturationBoundary();
};

const FVoxelLandChunkKey FVoxelLandM6StampShapeFixture::Key{FIntVector(-1, -1, 0)};

FVoxelLandM6Stamp FVoxelLandM6StampShapeFixture::MakeBox(
	uint32 Id, const FInt64Vector& Center, const FIntVector& HalfExtent)
{
	FVoxelLandM6Stamp Box;
	Box.Id = FGuid(0x4d36, 1, 0, Id);
	Box.Shape = EVoxelLandM6StampShape::Box;
	Box.OriginCm = Center;
	Box.HalfExtentCm = HalfExtent;
	return Box;
}

FVoxelLandM6Stamp FVoxelLandM6StampShapeFixture::MakeCapsule()
{
	FVoxelLandM6Stamp Capsule;
	Capsule.Id = FGuid(0x4d36, 1, 0, 2);
	Capsule.Shape = EVoxelLandM6StampShape::Capsule;
	Capsule.OriginCm = FInt64Vector(-600, -500, 200);
	Capsule.SegmentEndCm = FInt64Vector(-300, -100, 200);
	Capsule.RadiusCm = 100;
	return Capsule;
}

int16 FVoxelLandM6StampShapeFixture::Sample(const FVoxelLandChunkData& Chunk, const FIntVector& GlobalLattice)
{
	const FIntVector Local = GlobalLattice - Chunk.Key.Coordinate * FFixture::Cells;
	return Chunk.Density[FFixture::SampleIndex(Local, FFixture::Samples)];
}

bool FVoxelLandM6StampShapeFixture::Generate(FVoxelLandGeneratorSettings Settings, const FGridPtr& Grid,
	TConstArrayView<FVoxelLandM6Stamp> Stamps, FChunkPtr& Out, FGeneratorPtr& OutGenerator)
{
	Settings.M6Stamps.Stamps = Stamps;
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	return !FFixture::CreateGenerator(Settings, Grid, false, OutGenerator)
		&& !OutGenerator->CreateWorker(Worker) && !OutGenerator->GenerateChunk(Key, *Worker, Out);
}

bool FVoxelLandM6StampShapeFixture::RejectInvalid(const FVoxelLandGeneratorSettings& Settings, const FGridPtr& Grid,
	const FVoxelLandM6Stamp& Box, const FVoxelLandM6Stamp& Capsule, const FGeneratorPtr& Prior)
{
	FVoxelLandM6Stamp Invalid[] = {Box, Box, Capsule, Box, Capsule, Capsule, Box};
	Invalid[0].BlendRadiusCm = 1;
	Invalid[1].HalfExtentCm.Y = 0;
	Invalid[2].SegmentEndCm = Invalid[2].OriginCm;
	Invalid[3].OriginCm.X = MIN_int64;
	Invalid[4].SegmentEndCm.X = MAX_int64;
	Invalid[5].SegmentEndCm.X = Invalid[5].OriginCm.X + 2000001;
	Invalid[6].OriginCm.X = (int64(1) << 45) - 1;
	FGeneratorPtr Output = Prior;
	FVoxelLandGeneratorSettings Attempt = Settings;
	for (const FVoxelLandM6Stamp& Stamp : Invalid)
	{
		Attempt.M6Stamps.Stamps = MakeArrayView(&Stamp, 1);
		if (!FFixture::CreateGenerator(Attempt, Grid, false, Output) || Output != Prior)
		{
			return false;
		}
	}

	Attempt.M6Stamps.Stamps = MakeArrayView(&Box, 1);
	const FVoxelLandM6SphereStamp Legacy =
		FFixture::Stamp(1, EVoxelLandM6SphereOperation::Add, FIntVector::ZeroValue, 100);
	Attempt.M6Spheres.Stamps = MakeArrayView(&Legacy, 1);
	if (!FFixture::CreateGenerator(Attempt, Grid, false, Output) || Output != Prior)
	{
		return false;
	}
	Attempt.M6Spheres = {};
	Attempt.Profile.VoxelSizeCm = 134218;
	return bool(FFixture::CreateGenerator(Attempt, Grid, false, Output)) && Output == Prior;
}

bool FVoxelLandM6StampShapeFixture::CheckFoundation()
{
	FVoxelLandGeneratorSettings Settings;
	FGridPtr Grid;
	const FVoxelLandM6Stamp Clear = MakeBox(3, FInt64Vector(-800, -800, 350), FIntVector(300));
	const FVoxelLandM6Stamp Fill = MakeBox(4, FInt64Vector(-800, -800, 100), FIntVector(200, 200, 100));
	TArray<FVoxelLandM6Stamp> Foundation;
	if (!FFixture::CreateInputs(300, Settings, Grid)
		|| FVoxelLandM6StampComposition::AppendBoxFoundation(Clear, Fill, Foundation)
		|| Foundation.Num() != 2 || Foundation[0].Operation != EVoxelLandM6StampOperation::Subtract
		|| Foundation[1].Operation != EVoxelLandM6StampOperation::Add)
	{
		return false;
	}
	if (!FVoxelLandM6StampComposition::AppendBoxFoundation(Clear, Fill, Foundation) || Foundation.Num() != 2)
	{
		return false;
	}

	FChunkPtr Built, Reversed, Hole;
	FGeneratorPtr BuiltGenerator, ReversedGenerator, HoleGenerator;
	if (!Generate(Settings, Grid, Foundation, Built, BuiltGenerator))
	{
		return false;
	}
	Swap(Foundation[0], Foundation[1]);
	if (!Generate(Settings, Grid, Foundation, Reversed, ReversedGenerator))
	{
		return false;
	}
	const FVoxelLandM6Stamp HoleStamp = FVoxelLandM6StampComposition::CutHole(Clear);
	if (!Generate(Settings, Grid, MakeArrayView(&HoleStamp, 1), Hole, HoleGenerator))
	{
		return false;
	}
	const FIntVector FoundationInterior(-8, -8, 1);
	const FIntVector AboveFoundation(-8, -8, 3);
	const TConstArrayView<uint8> ForwardBytes = BuiltGenerator->GetM6StampIdentity();
	const TConstArrayView<uint8> ReverseBytes = ReversedGenerator->GetM6StampIdentity();
	return Sample(*Built, FoundationInterior) == -1024 && Sample(*Reversed, FoundationInterior) == 512
		&& Sample(*Hole, FoundationInterior) == 512 && Sample(*Built, AboveFoundation) > 0
		&& ForwardBytes.Num() == ReverseBytes.Num()
		&& FMemory::Memcmp(ForwardBytes.GetData(), ReverseBytes.GetData(), ForwardBytes.Num()) != 0;
}

bool FVoxelLandM6StampShapeFixture::CheckSaturationBoundary()
{
	FVoxelLandGeneratorSettings Settings;
	FGridPtr Grid;
	const FVoxelLandM6Stamp Cut = FVoxelLandM6StampComposition::CutHole(
		MakeBox(5, FInt64Vector(2300, -1000, 0), FIntVector(100)));
	FChunkPtr Chunk;
	FGeneratorPtr Generator;
	return FFixture::CreateInputs(12000, Settings, Grid)
		&& Generate(Settings, Grid, MakeArrayView(&Cut, 1), Chunk, Generator)
		&& Sample(*Chunk, FIntVector(-10, -10, 0)) == MIN_int16
		&& Sample(*Chunk, FIntVector(-9, -10, 0)) == -31744;
}

FString FVoxelLandM6StampShapeFixture::Run()
{
	FVoxelLandGeneratorSettings Settings;
	FGridPtr Grid;
	if (!FFixture::CreateInputs(-8000, Settings, Grid))
	{
		return TEXT("Finite baseline setup failed");
	}
	const FVoxelLandM6Stamp Box = MakeBox(1, FInt64Vector(-400, -500, 400), FIntVector(100, 200, 300));
	const FVoxelLandM6Stamp Capsule = MakeCapsule();
	FChunkPtr BoxChunk, CapsuleChunk;
	FGeneratorPtr BoxGenerator, CapsuleGenerator;
	if (!Generate(Settings, Grid, MakeArrayView(&Box, 1), BoxChunk, BoxGenerator)
		|| !Generate(Settings, Grid, MakeArrayView(&Capsule, 1), CapsuleChunk, CapsuleGenerator))
	{
		return TEXT("Ordinary finite box or capsule generation failed");
	}
	if (Sample(*BoxChunk, FIntVector(-4, -5, 4)) != -1024
		|| Sample(*BoxChunk, FIntVector(-3, -5, 4)) != 0
		|| Sample(*BoxChunk, FIntVector(-2, -2, 8)) != 1774)
	{
		return TEXT("Box interior, face or Euclidean corner density differs");
	}
	if (Sample(*CapsuleChunk, FIntVector(-5, -3, 2)) != -614
		|| Sample(*CapsuleChunk, FIntVector(-5, -2, 2)) != 0
		|| Sample(*CapsuleChunk, FIntVector(-8, -5, 2)) != 1024
		|| Sample(*CapsuleChunk, FIntVector(-3, 1, 2)) != 1024)
	{
		return TEXT("Capsule oblique projection, boundary or endpoint density differs");
	}
	if (!RejectInvalid(Settings, Grid, Box, Capsule, BoxGenerator) || !CheckFoundation() || !CheckSaturationBoundary())
	{
		return TEXT("Invalid parameters or atomic, ordered foundation behavior differ");
	}
	return {};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM6StampShapeTest, "VoxelLand.M6.Stamps.ShapesAndFoundation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM6StampShapeTest::RunTest(const FString& Parameters)
{
	const FString Error = Async(EAsyncExecution::ThreadPool, &FVoxelLandM6StampShapeFixture::Run).Get();
	return TestTrue(Error.IsEmpty() ? TEXT("Generation-time hard stamps and foundation") : Error, Error.IsEmpty());
}
#endif
