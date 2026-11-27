#if WITH_DEV_AUTOMATION_TESTS

#include "Generation/Placement/VoxelLandTerrainPlacement.h"
#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Misc/AutomationTest.h"

class FVoxelLandTerrainPlacementFixture final
{
public:
	static constexpr float VoxelSizeCm = 10.f;
	static constexpr double FloorHeightCm = 121.25;
	static constexpr double SpacingCm = 40.0;
	static constexpr int32 Seed = 7143;

	static bool NeverCancel() { return false; }
	static bool AlwaysCancel() { return true; }

	static FVoxelLandCollisionField::FChunkPtr MakeChunk(FIntVector Coordinate)
	{
		const TSharedRef<FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk =
			MakeShared<FVoxelLandChunkData, ESPMode::ThreadSafe>();
		constexpr int32 Cells = FVoxelLandChunkData::Cells;
		Chunk->Key.Coordinate = Coordinate;
		Chunk->Density.Reserve(FVoxelLandChunkData::SampleCount);
		for (int32 Z = -1; Z <= Cells + 1; ++Z)
		{
			const int32 GlobalZ = Coordinate.Z * Cells + Z;
			const int16 Density = static_cast<int16>((8 * GlobalZ - 97) * 64);
			for (int32 Y = -1; Y <= Cells + 1; ++Y)
			{
				for (int32 X = -1; X <= Cells + 1; ++X)
				{
					Chunk->Density.Add(Density);
				}
			}
		}
		return Chunk;
	}

	static bool Sample(const FVoxelLandCollisionField& Field,
		TConstArrayView<FVoxelLandCollisionField::FChunkPtr> Chunks, const FBox& Bounds,
		TArray<FVoxelLandTerrainPlacementPoint>& Out)
	{
		return FVoxelLandTerrainPlacement::Sample({Field, Chunks, Bounds}, &NeverCancel, SpacingCm, Seed, 40.0, Out);
	}

	static void CheckGroundAndSpacing(
		FAutomationTestBase& Test, TConstArrayView<FVoxelLandTerrainPlacementPoint> Samples)
	{
		for (int32 A = 0; A < Samples.Num(); ++A)
		{
			Test.TestTrue(TEXT("The point root lies on the completed flat surface"),
				FMath::IsNearlyEqual(Samples[A].Position.Z, FloorHeightCm, 0.1));
			Test.TestTrue(TEXT("The completed terrain normal points up"), Samples[A].Normal.Z > 0.999);
			for (int32 B = A + 1; B < Samples.Num(); ++B)
			{
				Test.TestTrue(TEXT("The lattice preserves horizontal spacing across the seam"),
					FVector::DistSquaredXY(Samples[A].Position, Samples[B].Position) >= FMath::Square(SpacingCm));
			}
		}
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandTerrainPlacementNodeTest, "VoxelLand.PCG.SurfacePlacement",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandTerrainPlacementNodeTest::RunTest(const FString&)
{
	using FFixture = FVoxelLandTerrainPlacementFixture;
	const FVoxelLandCollisionField::FChunkPtr Left = FFixture::MakeChunk(FIntVector(-1, 0, 0));
	const FVoxelLandCollisionField::FChunkPtr Right = FFixture::MakeChunk(FIntVector(0, 0, 0));
	const FVoxelLandCollisionField Field(FFixture::VoxelSizeCm, {{Left->Key, Left}, {Right->Key, Right}});
	const TArray<FVoxelLandCollisionField::FChunkPtr> Chunks = {Left, Right};
	const TArray<FVoxelLandCollisionField::FChunkPtr> Reversed = {Right, Left};
	const FBox Bounds(FVector(-320, 0, 0), FVector(320, 320, 320));
	const FBox LeftBounds(Bounds.Min, FVector(17, 320, 320));
	const FBox RightBounds(FVector(17, 0, 0), Bounds.Max);
	TArray<FVoxelLandTerrainPlacementPoint> Full;
	TArray<FVoxelLandTerrainPlacementPoint> OtherOrder;
	TArray<FVoxelLandTerrainPlacementPoint> SplitLeft;
	TArray<FVoxelLandTerrainPlacementPoint> SplitRight;
	if (!TestTrue(TEXT("The fixed field supplies a forest surface"), FFixture::Sample(Field, Chunks, Bounds, Full))
		|| !TestTrue(TEXT("Sampling does not depend on acquisition order"),
			FFixture::Sample(Field, Reversed, Bounds, OtherOrder))
		|| !TestTrue(TEXT("Left execution cell can use the shared lattice"),
			FFixture::Sample(Field, Chunks, LeftBounds, SplitLeft))
		|| !TestTrue(TEXT("Right execution cell can use the shared lattice"),
			FFixture::Sample(Field, Chunks, RightBounds, SplitRight)))
	{
		return false;
	}
	TestTrue(TEXT("The reference fixture produces points"), !Full.IsEmpty());
	TestEqual(TEXT("Chunk order preserves count"), OtherOrder.Num(), Full.Num());
	TestEqual(TEXT("An execution boundary crossing a lattice cell does not duplicate ownership"),
		SplitLeft.Num() + SplitRight.Num(), Full.Num());
	for (int32 Index = 0; Index < FMath::Min(Full.Num(), OtherOrder.Num()); ++Index)
	{
		TestTrue(TEXT("Chunk order preserves point identity and transform"),
			Full[Index].Seed == OtherOrder[Index].Seed && Full[Index].Position == OtherOrder[Index].Position);
	}
	FFixture::CheckGroundAndSpacing(*this, Full);
	TArray<FVoxelLandTerrainPlacementMesh> Meshes;
	TestTrue(TEXT("Build the exact geometry used by projection"),
		FVoxelLandTerrainPlacement::BuildMeshes({Field, Chunks, Bounds}, &FFixture::NeverCancel, Meshes));
	FVoxelLandTerrainPlacementPoint Hit;
	TestTrue(TEXT("Projection finds an upward facing surface"),
		FVoxelLandTerrainPlacement::Project(Meshes, FVector(100, 100, 190), FVector::DownVector, 120.0, true, Hit));
	TestTrue(TEXT("Projected height matches sampled terrain"),
		FMath::IsNearlyEqual(Hit.Position.Z, FFixture::FloorHeightCm, 0.1));
	TestTrue(TEXT("A proven miss may traverse available data"),
		FVoxelLandTerrainPlacement::CanProject(Field, FVector(100, 100, 190), FVector::UpVector, 120.0));
	TestFalse(TEXT("A ray into missing terrain cannot return a proven miss"),
		FVoxelLandTerrainPlacement::CanProject(Field, FVector(310, 100, 190), FVector::ForwardVector, 120.0));
	TArray<FVoxelLandTerrainPlacementPoint> Cancelled;
	TestFalse(TEXT("Cancellation does not return publishable samples"),
		FVoxelLandTerrainPlacement::Sample(
			{Field, Chunks, Bounds}, &FFixture::AlwaysCancel, FFixture::SpacingCm, FFixture::Seed, 40.0, Cancelled));
	TestTrue(TEXT("Cancellation leaves the output empty"), Cancelled.IsEmpty());
	return true;
}

#endif
