#if WITH_DEV_AUTOMATION_TESTS

#include "Generation/Collision/VoxelLandCollisionField.h"
#include "Misc/AutomationTest.h"
#include "World/Collision/VoxelLandWalkMotor.h"
#include "World/Collision/VoxelLandWalkSettings.h"
#include "World/Collision/VoxelLandWalkState.h"

class FVoxelLandWalkTransitionFixture final
{
public:
	using FDensity = double (*)(double X, double Z);
	static constexpr double VoxelCm = 10.0;
	static constexpr double WallCm = 160.0;
	static constexpr double LedgeCm = 150.0;
	static constexpr double StepCm = 20.0;
	static constexpr double Slope = 0.35;

	static double Wall(double X, double Z) { return FMath::Min(Z, WallCm - X); }
	static double Ledge(double X, double Z) { return FMath::Max(Z, X - LedgeCm); }
	static double Step(double X, double Z) { return Z - (X >= WallCm ? StepCm : 0.0); }
	static double Hill(double X, double Z) { return Z - Slope * (X - 100.0); }

	static FVoxelLandCollisionField MakeField(FDensity Density);
	static void Walk(FAutomationTestBase& Test, FDensity Density, const FVector3d& Start, const FVector2d& Input,
		int32 Steps, FVoxelLandWalkState& State);
};

FVoxelLandCollisionField FVoxelLandWalkTransitionFixture::MakeField(FDensity Density)
{
	constexpr int32 Samples = FVoxelLandChunkData::SamplesPerAxis;
	constexpr int32 Cells = FVoxelLandChunkData::Cells;
	FVoxelLandCollisionField::FChunkMap Chunks;
	for (int32 TileZ = -1; TileZ <= 0; ++TileZ)
	{
		for (int32 TileY = 0; TileY <= 1; ++TileY)
		{
			for (int32 TileX = 0; TileX <= 1; ++TileX)
			{
				const TSharedRef<FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk =
					MakeShared<FVoxelLandChunkData, ESPMode::ThreadSafe>();
				Chunk->Key.Coordinate = FIntVector(TileX, TileY, TileZ);
				Chunk->Density.Reserve(FVoxelLandChunkData::SampleCount);
				for (int32 Z = 0; Z < Samples; ++Z)
				{
					for (int32 Y = 0; Y < Samples; ++Y)
					{
						for (int32 X = 0; X < Samples; ++X)
						{
							const double Value =
								Density(VoxelCm * (TileX * Cells + X - 1), VoxelCm * (TileZ * Cells + Z - 1));
							Chunk->Density.Add(
								static_cast<int16>(FMath::Clamp(FMath::RoundToInt(Value / VoxelCm * 1024.0),
									static_cast<int32>(MIN_int16), static_cast<int32>(MAX_int16))));
						}
					}
				}
				Chunks.Add(Chunk->Key, Chunk);
			}
		}
	}
	return FVoxelLandCollisionField(VoxelCm, MoveTemp(Chunks));
}

void FVoxelLandWalkTransitionFixture::Walk(FAutomationTestBase& Test, FDensity Density, const FVector3d& Start,
	const FVector2d& Input, int32 Steps, FVoxelLandWalkState& State)
{
	const FVoxelLandCollisionField Field = MakeField(Density);
	const FVoxelLandWalkSettings Settings;
	State.Center = Start;
	int32 Blocked = 0;
	for (int32 Index = 0; Index < Steps; ++Index)
	{
		Blocked += FVoxelLandWalkMotor::Step(Field, Settings, Input, State) ? 0 : 1;
	}
	Test.TestEqual(
		FString::Printf(TEXT("Resident trajectory from %s reaches %s (grounded %d) with no unavailable steps"),
			*Start.ToString(), *State.Center.ToString(), int32(State.bIsGrounded)),
		Blocked, 0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandWalkTransitionTest, "VoxelLand.Collision.WalkTransitions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandWalkTransitionTest::RunTest(const FString& Parameters)
{
	using FWalkFixture = FVoxelLandWalkTransitionFixture;
	constexpr double StartAboveGroundCm = 94.0;
	FVoxelLandWalkState Wall;
	FWalkFixture::Walk(
		*this, FWalkFixture::Wall, FVector3d(60.0, 100.0, StartAboveGroundCm), FVector2d(1.0, 1.0), 40, Wall);
	TestTrue(TEXT("Wall stops the capsule's X advance and allows Y sliding"),
		Wall.Center.X <= FWalkFixture::WallCm - 34.0 && Wall.Center.Y > 260.0 && Wall.bIsGrounded);

	FVoxelLandWalkState Ledge;
	FWalkFixture::Walk(
		*this, FWalkFixture::Ledge, FVector3d(80.0, 160.0, StartAboveGroundCm), FVector2d(1.0, 0.0), 36, Ledge);
	TestTrue(TEXT("Walking over the ledge starts a fall"),
		Ledge.Center.X > FWalkFixture::LedgeCm + 34.0 && Ledge.Center.Z < 70.0 && !Ledge.bIsGrounded);

	FVoxelLandWalkState Step;
	FWalkFixture::Walk(
		*this, FWalkFixture::Step, FVector3d(80.0, 160.0, StartAboveGroundCm), FVector2d(1.0, 0.0), 30, Step);
	TestTrue(TEXT("Capsule walks up a short step and remains grounded"),
		Step.Center.X > FWalkFixture::WallCm + 34.0 && Step.Center.Z > 108.0 && Step.bIsGrounded);

	FVoxelLandWalkState Hill;
	FWalkFixture::Walk(
		*this, FWalkFixture::Hill, FVector3d(100.0, 160.0, StartAboveGroundCm + 5.0), FVector2d(1.0, 0.0), 24, Hill);
	TestTrue(TEXT("Ground-projected movement ascends a continuous slope"),
		Hill.Center.X > 220.0 && Hill.Center.Z > 135.0 && Hill.bIsGrounded);
	return true;
}

#endif
