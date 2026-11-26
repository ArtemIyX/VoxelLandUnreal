#if WITH_DEV_AUTOMATION_TESTS

#include "../TerrainOverlay/Stamps/VoxelLandTerrainSphereTestFixture.h"

#include "Generation/Collision/VoxelLandCollisionField.h"
#include "Generation/Collision/VoxelLandSdfCollision.h"
#include "Async/Async.h"
#include "Misc/AutomationTest.h"

class FVoxelLandSdfCollisionTestFixture final
{
public:
	using FDensity = int16 (*)(const FIntVector&);
	static constexpr double VoxelSizeCm = FVoxelLandM6SphereTestFixture::VoxelSizeCm;
	static constexpr double FloorCm = 25.0;
	static constexpr double RadiusCm = 25.0;
	static constexpr double HalfHeightCm = 60.0;
	static constexpr double SkinCm = 1.0;
	static constexpr double FloorContactCm = FloorCm + HalfHeightCm + SkinCm;
	static constexpr double ContactToleranceCm = 0.25;

	static FVoxelLandCapsuleSweepRequest Request(const FVector3d& Start, const FVector3d& End)
	{
		FVoxelLandCapsuleSweepRequest Sweep;
		Sweep.Start = Start;
		Sweep.End = End;
		Sweep.RadiusCm = RadiusCm;
		Sweep.HalfHeightCm = HalfHeightCm;
		Sweep.SkinCm = SkinCm;
		Sweep.ContactToleranceCm = ContactToleranceCm;
		return Sweep;
	}

	static int16 Air(const FIntVector&) { return 1024; }
	static int16 Plane(const FIntVector& Point)
	{
		return static_cast<int16>(FMath::Clamp(Point.Z * 1024 - 256, int32(MIN_int16), int32(MAX_int16)));
	}
	static int16 Slope(const FIntVector& Point)
	{
		return static_cast<int16>(
			FMath::Clamp(Point.Z * 1024 - Point.X * 512 - 256, int32(MIN_int16), int32(MAX_int16)));
	}
	static int16 ThinWall(const FIntVector& Point)
	{
		// Large positive magnitudes cannot safely serve as travel distances.
		return Point.X == 4 ? -16 : MAX_int16;
	}

	static FVoxelLandCollisionField::FChunkPtr MakeChunk(int32 ChunkX, int32 ChunkZ, FDensity Density);
	static FVoxelLandCollisionField::FChunkMap PlaneChunks(FDensity Density);
	static FVoxelLandCollisionField::FChunkMap GenerateSeam(bool bReverseOrder);
};

FVoxelLandCollisionField::FChunkPtr FVoxelLandSdfCollisionTestFixture::MakeChunk(
	int32 ChunkX, int32 ChunkZ, FDensity Density)
{
	constexpr int32 Cells = FVoxelLandChunkData::Cells;
	constexpr int32 SampleAxis = FVoxelLandChunkData::SamplesPerAxis;
	TSharedRef<FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk = MakeShared<FVoxelLandChunkData, ESPMode::ThreadSafe>();
	Chunk->Key.Coordinate = FIntVector(ChunkX, 0, ChunkZ);
	Chunk->Density.Reserve(FVoxelLandChunkData::SampleCount);
	for (int32 Z = -1; Z < SampleAxis - 1; ++Z)
	{
		for (int32 Y = -1; Y < SampleAxis - 1; ++Y)
		{
			for (int32 X = -1; X < SampleAxis - 1; ++X)
			{
				Chunk->Density.Add(Density(FIntVector(ChunkX * Cells + X, Y, ChunkZ * Cells + Z)));
			}
		}
	}
	return Chunk;
}

FVoxelLandCollisionField::FChunkMap FVoxelLandSdfCollisionTestFixture::PlaneChunks(FDensity Density)
{
	FVoxelLandCollisionField::FChunkMap Result;
	for (const int32 ChunkX : {-1, 0})
	{
		for (const int32 ChunkZ : {-1, 0})
		{
			const FVoxelLandCollisionField::FChunkPtr Chunk = MakeChunk(ChunkX, ChunkZ, Density);
			Result.Add(Chunk->Key, Chunk);
		}
	}
	return Result;
}

FVoxelLandCollisionField::FChunkMap FVoxelLandSdfCollisionTestFixture::GenerateSeam(bool bReverseOrder)
{
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	FVoxelLandGeneratorSettings Settings;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
	if (!FVoxelLandM6SphereTestFixture::CreateInputs(static_cast<int32>(FloorCm), Settings, Grid)
		|| FVoxelLandM6SphereTestFixture::CreateGenerator(Settings, Grid, false, Generator))
	{
		return {};
	}
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	if (Generator->CreateWorker(Worker))
	{
		return {};
	}
	FVoxelLandCollisionField::FChunkMap Result;
	const int32 ChunkCoordinates[] = {bReverseOrder ? 0 : -1, bReverseOrder ? -1 : 0};
	for (const int32 X : ChunkCoordinates)
	{
		FVoxelLandChunkKey Key;
		Key.Coordinate = FIntVector(X, 0, 0);
		FVoxelLandCollisionField::FChunkPtr Data;
		if (Generator->GenerateChunk(Key, *Worker, Data) || !Data)
		{
			return {};
		}
		Result.Add(Key, MoveTemp(Data));
	}
	return Result;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandSdfCapsuleContactTest, "VoxelLand.Collision.Capsule.ContactAndBounds",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandSdfCapsuleContactTest::RunTest(const FString& Parameters)
{
	using FCollisionFixture = FVoxelLandSdfCollisionTestFixture;
	const FVoxelLandCollisionField Field(
		FCollisionFixture::VoxelSizeCm, FCollisionFixture::PlaneChunks(FCollisionFixture::Plane));
	const FVoxelLandCapsuleSweepRequest Down =
		FCollisionFixture::Request(FVector3d(-50, 120, 220), FVector3d(-50, 120, 0));
	const double Expected = (Down.Start.Z - FCollisionFixture::FloorContactCm) / (Down.Start.Z - Down.End.Z);
	const FVoxelLandCapsuleSweepResult Impact = FVoxelLandSdfCollision::SweepCapsule(Field, Down);
	TestEqual(TEXT("Downward capsule contacts floor"), Impact.Status, EVoxelLandCapsuleSweepStatus::Contact);
	TestTrue(TEXT("Contact witness brackets the analytic plane"),
		FMath::Abs(Impact.Time - Expected) * Down.Start.Z <= FCollisionFixture::ContactToleranceCm
			&& Impact.LastSafeTime <= Expected && Impact.LastSafeTime <= Impact.Time);
	TestTrue(TEXT("Floor normal points out of the field"), Impact.Normal.Equals(FVector3d::UpVector));

	FVoxelLandCapsuleSweepRequest EndsOnFloor = Down;
	EndsOnFloor.End.Z = FCollisionFixture::FloorContactCm;
	const FVoxelLandCapsuleSweepResult Endpoint = FVoxelLandSdfCollision::SweepCapsule(Field, EndsOnFloor);
	TestEqual(
		TEXT("Closed sweep includes an exact end contact"), Endpoint.Status, EVoxelLandCapsuleSweepStatus::Contact);
	TestTrue(TEXT("Endpoint time is close to one"), FMath::IsNearlyEqual(Endpoint.Time, 1.0, 0.003));

	FVoxelLandCapsuleSweepRequest Overlap = Down;
	Overlap.Start.Z = FCollisionFixture::FloorContactCm - FCollisionFixture::SkinCm * 0.5;
	const FVoxelLandCapsuleSweepResult SkinOnly = FVoxelLandSdfCollision::OverlapCapsule(Field, Overlap);
	TestEqual(TEXT("Skin-only overlap reports contact"), SkinOnly.Status, EVoxelLandCapsuleSweepStatus::Contact);
	TestEqual(TEXT("Skin-only contact leaves no allowed advance"), SkinOnly.LastSafeTime, 0.0);

	Overlap.Start.Z = FCollisionFixture::FloorContactCm - FCollisionFixture::SkinCm - 10.0;
	const FVoxelLandCapsuleSweepResult Penetrating = FVoxelLandSdfCollision::OverlapCapsule(Field, Overlap);
	TestEqual(TEXT("Strict physical overlap is reported at the initial pose"), Penetrating.Status,
		EVoxelLandCapsuleSweepStatus::StartPenetrating);
	TestEqual(TEXT("Penetration leaves no allowed advance"), Penetrating.LastSafeTime, 0.0);

	FVoxelLandCapsuleSweepRequest Limited = Down;
	Limited.CellBudget = 1;
	const FVoxelLandCapsuleSweepResult Exhausted = FVoxelLandSdfCollision::SweepCapsule(Field, Limited);
	TestEqual(TEXT("Exhausted broadphase blocks"), Exhausted.Status, EVoxelLandCapsuleSweepStatus::Indeterminate);
	TestEqual(TEXT("Unvisited cells admit no motion"), Exhausted.LastSafeTime, 0.0);

	FVoxelLandCapsuleSweepRequest Invalid = Down;
	Invalid.HalfHeightCm = Invalid.RadiusCm * 0.5;
	TestEqual(TEXT("Invalid capsule dimensions are rejected"),
		FVoxelLandSdfCollision::SweepCapsule(Field, Invalid).Status, EVoxelLandCapsuleSweepStatus::Invalid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandSdfCapsuleAdversarialTest,
	"VoxelLand.Collision.Capsule.NonDistanceAndAvailability",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandSdfCapsuleAdversarialTest::RunTest(const FString& Parameters)
{
	using FCollisionFixture = FVoxelLandSdfCollisionTestFixture;
	const FVoxelLandCollisionField Slope(
		FCollisionFixture::VoxelSizeCm, FCollisionFixture::PlaneChunks(FCollisionFixture::Slope));
	const FVoxelLandCapsuleSweepRequest Down =
		FCollisionFixture::Request(FVector3d(50, 125, 220), FVector3d(50, 125, 0));
	const FVoxelLandCapsuleSweepResult Incline = FVoxelLandSdfCollision::SweepCapsule(Slope, Down);
	const FVector3d ExpectedNormal = FVector3d(-0.5, 0.0, 1.0).GetSafeNormal();
	TestEqual(FString::Printf(TEXT("Oblique floor is hit (status %d, time %.6f, safe %.6f, cells %d, regions %d)"),
				  static_cast<int32>(Incline.Status), Incline.Time, Incline.LastSafeTime, Incline.CellsVisited,
				  Incline.RegionsVisited),
		Incline.Status, EVoxelLandCapsuleSweepStatus::Contact);
	TestTrue(TEXT("Slope normal follows the completed field gradient"), Incline.Normal.Equals(ExpectedNormal, 0.001));

	const FVoxelLandCollisionField Wall(
		FCollisionFixture::VoxelSizeCm, FCollisionFixture::PlaneChunks(FCollisionFixture::ThinWall));
	const FVoxelLandCapsuleSweepRequest Fast =
		FCollisionFixture::Request(FVector3d(100, 200, 500), FVector3d(1700, 200, 500));
	const FVoxelLandCapsuleSweepResult WallHit = FVoxelLandSdfCollision::SweepCapsule(Wall, Fast);
	const double WallSampleTime =
		(400.0 - FCollisionFixture::RadiusCm - FCollisionFixture::SkinCm - Fast.Start.X) / (Fast.End.X - Fast.Start.X);
	TestEqual(TEXT("Thin wall with exaggerated density still blocks fast movement"), WallHit.Status,
		EVoxelLandCapsuleSweepStatus::Contact);
	TestTrue(TEXT("Safe motion stops before the solid wall sample"),
		WallHit.LastSafeTime <= WallSampleTime && WallHit.Time <= WallSampleTime + 0.001);

	FVoxelLandCollisionField::FChunkMap Half;
	const FVoxelLandCollisionField::FChunkPtr Left = FCollisionFixture::MakeChunk(-1, 0, FCollisionFixture::Air);
	Half.Add(Left->Key, Left);
	const FVoxelLandCollisionField Frontier(FCollisionFixture::VoxelSizeCm, MoveTemp(Half));
	const FVoxelLandCapsuleSweepRequest Traverse =
		FCollisionFixture::Request(FVector3d(-110, 200, 200), FVector3d(110, 200, 200));
	const FVoxelLandCapsuleSweepResult Unavailable = FVoxelLandSdfCollision::SweepCapsule(Frontier, Traverse);
	const double FrontierTime = (-FCollisionFixture::RadiusCm - FCollisionFixture::SkinCm - Traverse.Start.X)
							  / (Traverse.End.X - Traverse.Start.X);
	TestEqual(
		TEXT("Absent canonical owner blocks the sweep"), Unavailable.Status, EVoxelLandCapsuleSweepStatus::Unavailable);
	TestTrue(TEXT("Result includes the first missing owner"),
		Unavailable.MissingKey.IsSet() && Unavailable.MissingKey->Coordinate == FIntVector::ZeroValue);
	TestTrue(TEXT("Known air before an unloaded chunk is safe"),
		Unavailable.LastSafeTime <= FrontierTime
			&& FMath::IsNearlyEqual(Unavailable.LastSafeTime, FrontierTime, 0.001));

	const FVoxelLandCollisionField Air(
		FCollisionFixture::VoxelSizeCm, FCollisionFixture::PlaneChunks(FCollisionFixture::Air));
	const FVoxelLandCapsuleSweepResult Clear = FVoxelLandSdfCollision::SweepCapsule(Air, Traverse);
	TestEqual(TEXT("The same sweep clears after the owner arrives"), Clear.Status, EVoxelLandCapsuleSweepStatus::Clear);
	TestEqual(TEXT("A clear sweep admits full motion"), Clear.LastSafeTime, 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandSdfCapsuleRealSeamTest, "VoxelLand.Collision.Capsule.CompletedHeightSeam",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandSdfCapsuleRealSeamTest::RunTest(const FString& Parameters)
{
	using FCollisionFixture = FVoxelLandSdfCollisionTestFixture;
	FVoxelLandCollisionField::FChunkMap Chunks =
		Async(EAsyncExecution::ThreadPool, [] { return FCollisionFixture::GenerateSeam(false); }).Get();
	FVoxelLandCollisionField::FChunkMap Reverse =
		Async(EAsyncExecution::ThreadPool, [] { return FCollisionFixture::GenerateSeam(true); }).Get();
	if (!TestEqual(TEXT("Two actual finite Height chunks were generated"), Chunks.Num(), 2)
		|| !TestEqual(TEXT("Reverse generation also succeeded"), Reverse.Num(), 2))
	{
		return false;
	}
	for (const TPair<FVoxelLandChunkKey, FVoxelLandCollisionField::FChunkPtr>& Entry : Chunks)
	{
		const FVoxelLandCollisionField::FChunkPtr* Other = Reverse.Find(Entry.Key);
		TestTrue(
			TEXT("Completed hash is stable across generation order"), Other && (*Other)->Hash == Entry.Value->Hash);
	}
	const FVoxelLandCollisionField A(FCollisionFixture::VoxelSizeCm, MoveTemp(Chunks));
	const FVoxelLandCollisionField B(FCollisionFixture::VoxelSizeCm, MoveTemp(Reverse));
	const FVoxelLandCapsuleSweepRequest Across =
		FCollisionFixture::Request(FVector3d(-120, 200, 100), FVector3d(120, 200, 100));
	TestEqual(TEXT("A capsule just above the floor passes across the negative/positive seam"),
		FVoxelLandSdfCollision::SweepCapsule(A, Across).Status, EVoxelLandCapsuleSweepStatus::Clear);
	for (const double X : {-35.0, 0.0, 35.0})
	{
		const FVoxelLandCapsuleSweepRequest Down =
			FCollisionFixture::Request(FVector3d(X, 200, 150), FVector3d(X, 200, 70));
		const FVoxelLandCapsuleSweepResult Forward = FVoxelLandSdfCollision::SweepCapsule(A, Down);
		const FVoxelLandCapsuleSweepResult Reversed = FVoxelLandSdfCollision::SweepCapsule(B, Down);
		TestEqual(TEXT("Ground query hits on and around the actual seam"), Forward.Status,
			EVoxelLandCapsuleSweepStatus::Contact);
		TestTrue(TEXT("Ground point stays on the same planar surface"),
			FMath::IsNearlyEqual(Forward.Point.Z, FCollisionFixture::FloorCm, FCollisionFixture::ContactToleranceCm));
		TestTrue(TEXT("Contact is independent of resident insertion order"),
			Forward.Status == Reversed.Status && Forward.Time == Reversed.Time
				&& Forward.LastSafeTime == Reversed.LastSafeTime && Forward.Point == Reversed.Point
				&& Forward.Normal == Reversed.Normal);
	}
	return true;
}

#endif
