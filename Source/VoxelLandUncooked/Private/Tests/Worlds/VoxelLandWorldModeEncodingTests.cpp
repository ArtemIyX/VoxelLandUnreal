#if WITH_DEV_AUTOMATION_TESTS
#include "World/Persistence/VoxelLandWorldModeEncoding.h"

#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Data/World/Configuration/VoxelLandWorldSourceSnapshot.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandWorldModeEncodingTest, "VoxelLand.WorldModes.Encoding",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandWorldModeEncodingTest::RunTest(const FString& Parameters)
{
	FVoxelLandFiniteWorldSnapshot Finite;
	Finite.Finite.OriginXCm = -2;
	FVoxelLandWorldSourceSnapshot FiniteSource(MoveTemp(Finite));
	FCanonicalWriter FiniteBytes;
	VoxelLandWorldModeEncoding::Write(FiniteBytes, FiniteSource);
	if (!TestTrue(TEXT("finite encoding has header and coordinates"), FiniteBytes.Bytes.Num() >= 17))
	{
		return false;
	}
	const uint8 ExpectedPrefix[] = {'V', 'W', 'M', 'D', 1, 0, 0, 0, uint8(EVoxelLandWorldKind::Finite), 0xfe, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ExpectedPrefix); ++Index)
	{
		TestEqual(TEXT("finite header and signed origin bytes"), FiniteBytes.Bytes[Index], ExpectedPrefix[Index]);
	}
	FVoxelLandInfiniteWorldSnapshot Infinite;
	Infinite.Infinite.RegionSizeCm = 409600;
	FVoxelLandWorldSourceSnapshot InfiniteSource(MoveTemp(Infinite));
	FCanonicalWriter InfiniteBytes;
	VoxelLandWorldModeEncoding::Write(InfiniteBytes, InfiniteSource);
	if (!TestTrue(TEXT("infinite encoding has header"), InfiniteBytes.Bytes.Num() >= 9))
	{
		return false;
	}
	TestEqual(TEXT("infinite mode tag"), InfiniteBytes.Bytes[8], uint8(EVoxelLandWorldKind::Infinite));
	TestTrue(TEXT("world modes have distinct encodings"), FiniteBytes.Bytes != InfiniteBytes.Bytes);
	FVoxelLandInfiniteWorldSnapshot Changed;
	Changed.Infinite.RegionSizeCm = 409601;
	FVoxelLandWorldSourceSnapshot ChangedSource(MoveTemp(Changed));
	FCanonicalWriter ChangedBytes;
	VoxelLandWorldModeEncoding::Write(ChangedBytes, ChangedSource);
	TestTrue(TEXT("region size affects encoded settings"), InfiniteBytes.Bytes != ChangedBytes.Bytes);
	FVoxelLandInfiniteWorldSnapshot ChangedSeed;
	ChangedSeed.Infinite.RegionSizeCm = 409600;
	ChangedSeed.Common.Seed += 1;
	FVoxelLandWorldSourceSnapshot ChangedSeedSource(MoveTemp(ChangedSeed));
	FCanonicalWriter ChangedSeedBytes;
	VoxelLandWorldModeEncoding::Write(ChangedSeedBytes, ChangedSeedSource);
	TestTrue(TEXT("world seed affects encoded settings"), InfiniteBytes.Bytes != ChangedSeedBytes.Bytes);
	FVoxelLandInfiniteWorldSnapshot ChangedFilter;
	ChangedFilter.Infinite.RegionSizeCm = 409600;
	ChangedFilter.Common.bSmoothEnabled = true;
	FVoxelLandWorldSourceSnapshot ChangedFilterSource(MoveTemp(ChangedFilter));
	FCanonicalWriter ChangedFilterBytes;
	VoxelLandWorldModeEncoding::Write(ChangedFilterBytes, ChangedFilterSource);
	TestTrue(TEXT("density filter affects encoded settings"), InfiniteBytes.Bytes != ChangedFilterBytes.Bytes);
	FVoxelLandInfiniteWorldSnapshot ChangedRoof;
	ChangedRoof.Infinite.RegionSizeCm = 409600;
	ChangedRoof.Common.Caves.RoofThicknessCm += 1;
	FVoxelLandWorldSourceSnapshot ChangedRoofSource(MoveTemp(ChangedRoof));
	FCanonicalWriter ChangedRoofBytes;
	VoxelLandWorldModeEncoding::Write(ChangedRoofBytes, ChangedRoofSource);
	TestTrue(TEXT("roof thickness affects encoded settings"), InfiniteBytes.Bytes != ChangedRoofBytes.Bytes);
	FVoxelLandInfiniteWorldSnapshot Presentation;
	Presentation.Infinite.RegionSizeCm = 409600;
	Presentation.Common.HorizontalRadius = 7;
	Presentation.Common.VerticalRadius = 3;
	Presentation.Common.MaterialSetupPath = FSoftObjectPath(TEXT("/Game/Test/Material.Material"));
	FVoxelLandWorldSourceSnapshot PresentationSource(MoveTemp(Presentation));
	FCanonicalWriter PresentationBytes;
	VoxelLandWorldModeEncoding::Write(PresentationBytes, PresentationSource);
	TestTrue(TEXT("presentation and demand do not affect generation settings"),
		InfiniteBytes.Bytes == PresentationBytes.Bytes);
	return true;
}
#endif
