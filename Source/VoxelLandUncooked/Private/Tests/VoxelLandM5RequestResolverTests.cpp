#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Structs/VoxelLandChunkKey.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandM5RequestResolver.h"
#include "World/VoxelLandM5ResolvedRequest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5RequestResolverTest, "VoxelLand.M5.Runtime.RequestResolution",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5RequestResolverTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5WorldSnapshot Snapshot;
	FVoxelLandM5PointCm Location;
	FVoxelLandM5ResolvedRequest Request;
	const TArray<uint8> Identity{1, 2, 3};
	TestEqual(TEXT("Disabled request succeeds"),
		FVoxelLandM5RequestResolver::Resolve(Snapshot, Location, {}, Request).Code, EVoxelLandM5ErrorCode::None);
	TestTrue(TEXT("Disabled request is marked disabled"), Request.IsDisabled());

	Snapshot.Caves.bEnabled = true;
	Snapshot.Caves.Bounds.MinInclusive.X = -100;
	Snapshot.Caves.Bounds.MinInclusive.Y = -100;
	Snapshot.Caves.Bounds.MinInclusive.Z = -100;
	Snapshot.Caves.Bounds.MaxExclusive.X = 100;
	Snapshot.Caves.Bounds.MaxExclusive.Y = 100;
	Snapshot.Caves.Bounds.MaxExclusive.Z = 100;
	Location.X = -100;
	TestEqual(TEXT("Minimum is included"),
		FVoxelLandM5RequestResolver::Resolve(Snapshot, Location, Identity, Request).Code, EVoxelLandM5ErrorCode::None);
	TestFalse(TEXT("Enabled request is not disabled"), Request.IsDisabled());
	TestEqual(TEXT("Identity retained"), Request.GetIdentity().Num(), Identity.Num());
	Location.X = 99;
	FVoxelLandM5ResolvedRequest Second;
	TestEqual(TEXT("Finite request at another location"),
		FVoxelLandM5RequestResolver::Resolve(Snapshot, Location, Identity, Second).Code, EVoxelLandM5ErrorCode::None);
	TestTrue(TEXT("Finite key is location independent"),
		Request.GetIdentity().Num() == Second.GetIdentity().Num()
			&& FMemory::Memcmp(
				   Request.GetIdentity().GetData(), Second.GetIdentity().GetData(), Request.GetIdentity().Num())
				   == 0);
	Location.X = 100;
	TestEqual(TEXT("Maximum is excluded"),
		FVoxelLandM5RequestResolver::Resolve(Snapshot, Location, Identity, Request).Code,
		EVoxelLandM5ErrorCode::OutOfDomain);
	TestEqual(TEXT("Failure preserves previous identity"), Request.GetIdentity().Num(), Identity.Num());
	Location.X = 0;
	TArray<uint8> MaxIdentity;
	MaxIdentity.SetNumZeroed(8 * 1024 * 1024 + 4096);
	TestEqual(TEXT("Maximum supported identity accepted"),
		FVoxelLandM5RequestResolver::Resolve(Snapshot, Location, MaxIdentity, Request).Code,
		EVoxelLandM5ErrorCode::None);
	MaxIdentity.Add(0);
	TestEqual(TEXT("Oversized identity rejected"),
		FVoxelLandM5RequestResolver::Resolve(Snapshot, Location, MaxIdentity, Request).Code,
		EVoxelLandM5ErrorCode::InvalidInput);
	Snapshot.Caves.PlanningMode = EVoxelLandM5PlanningMode::Regional;
	TestEqual(TEXT("Regional closure fails closed"),
		FVoxelLandM5RequestResolver::Resolve(Snapshot, Location, Identity, Request).Code,
		EVoxelLandM5ErrorCode::Unproven);

	Snapshot.Caves.PlanningMode = EVoxelLandM5PlanningMode::Finite;
	Snapshot.Base.VoxelSizeCm = 1;
	FVoxelLandM5Bounds Influence;
	Influence.MinInclusive.X = 34;
	Influence.MinInclusive.Y = 0;
	Influence.MinInclusive.Z = 0;
	Influence.MaxExclusive.X = 35;
	Influence.MaxExclusive.Y = 1;
	Influence.MaxExclusive.Z = 1;
	FVoxelLandChunkKey Key;
	bool bIntersects = false;
	TestEqual(TEXT("Halo footprint evaluated"),
		FVoxelLandM5RequestResolver::IntersectsStoredSamples(Snapshot, Influence, Key, bIntersects).Code,
		EVoxelLandM5ErrorCode::None);
	TestTrue(TEXT("Filter halo overlaps"), bIntersects);
	Influence.MinInclusive.X = 35;
	Influence.MaxExclusive.X = 36;
	TestEqual(TEXT("Outside footprint evaluated"),
		FVoxelLandM5RequestResolver::IntersectsStoredSamples(Snapshot, Influence, Key, bIntersects).Code,
		EVoxelLandM5ErrorCode::None);
	TestFalse(TEXT("Outside filter halo"), bIntersects);
	Key.Coordinate.X = -1;
	Influence.MinInclusive.X = -35;
	Influence.MaxExclusive.X = -34;
	TestEqual(TEXT("Negative chunk lower exclusive boundary evaluated"),
		FVoxelLandM5RequestResolver::IntersectsStoredSamples(Snapshot, Influence, Key, bIntersects).Code,
		EVoxelLandM5ErrorCode::None);
	TestFalse(TEXT("Negative chunk excludes interval ending at first sample"), bIntersects);
	Influence.MinInclusive.X = -34;
	Influence.MaxExclusive.X = -33;
	TestEqual(TEXT("Negative chunk first sample evaluated"),
		FVoxelLandM5RequestResolver::IntersectsStoredSamples(Snapshot, Influence, Key, bIntersects).Code,
		EVoxelLandM5ErrorCode::None);
	TestTrue(TEXT("Negative chunk includes first halo sample"), bIntersects);
	Influence.MinInclusive.X = 2;
	Influence.MaxExclusive.X = 3;
	TestEqual(TEXT("Negative chunk last sample evaluated"),
		FVoxelLandM5RequestResolver::IntersectsStoredSamples(Snapshot, Influence, Key, bIntersects).Code,
		EVoxelLandM5ErrorCode::None);
	TestTrue(TEXT("Negative chunk includes last halo sample"), bIntersects);
	Influence.MinInclusive.X = 3;
	Influence.MaxExclusive.X = 4;
	TestEqual(TEXT("Negative chunk upper boundary evaluated"),
		FVoxelLandM5RequestResolver::IntersectsStoredSamples(Snapshot, Influence, Key, bIntersects).Code,
		EVoxelLandM5ErrorCode::None);
	TestFalse(TEXT("Negative chunk excludes interval after last sample"), bIntersects);
	Key.LOD = 1;
	bIntersects = true;
	TestEqual(TEXT("Unsupported LOD rejected"),
		FVoxelLandM5RequestResolver::IntersectsStoredSamples(Snapshot, Influence, Key, bIntersects).Code,
		EVoxelLandM5ErrorCode::InvalidInput);
	TestTrue(TEXT("Error preserves intersection output"), bIntersects);
	Key.LOD = 0;
	Key.Coordinate.Z = MAX_int32;
	Snapshot.Base.VoxelSizeCm = MAX_int32;
	TestEqual(TEXT("Footprint overflow rejected"),
		FVoxelLandM5RequestResolver::IntersectsStoredSamples(Snapshot, Influence, Key, bIntersects).Code,
		EVoxelLandM5ErrorCode::ArithmeticOverflow);
	TestTrue(TEXT("Overflow preserves intersection output"), bIntersects);
	Snapshot.Caves.bEnabled = false;
	TestEqual(TEXT("Disabled has no barrier"),
		FVoxelLandM5RequestResolver::IntersectsStoredSamples(Snapshot, Influence, Key, bIntersects).Code,
		EVoxelLandM5ErrorCode::None);
	TestFalse(TEXT("Disabled does not intersect"), bIntersects);
	return true;
}

#endif
