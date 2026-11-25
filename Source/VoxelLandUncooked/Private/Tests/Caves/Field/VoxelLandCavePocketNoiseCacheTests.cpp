#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Field/VoxelLandCavePocketNoise.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5PocketNoiseCacheTest, "VoxelLand.M5.Pockets.CachedReference",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5PocketNoiseCacheTest::RunTest(const FString& Parameters)
{
	const auto NotCancelled = []() {
		return false;
	};
	const FGuid Stage(0x01234567u, 0x89abcdefu, 0x10203040u, 0xfedcba98u);
	constexpr uint64 Salt = 0x0102030405060708ULL;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	TestTrue(TEXT("budget"), FVoxelLandM5Budget::TryCreate(10000000, 10000000, Budget));
	if (!Budget)
	{
		return false;
	}
	FVoxelLandM5PointCm Min, Max;
	Min.X = -64;
	Min.Y = -64;
	Min.Z = -64;
	Max.X = 64;
	Max.Y = 64;
	Max.Z = 64;
	TUniquePtr<FVoxelLandM5PocketNoiseCache> Cache;
	TestEqual(TEXT("cache admitted"),
		FVoxelLandM5PocketNoiseCache::Build(Min, Max, 16, -1, Stage, Salt, Budget.ToSharedRef(), NotCancelled, Cache)
			.Code,
		EVoxelLandM5ErrorCode::None);
	if (!Cache)
	{
		return false;
	}
	for (int64 X : {-64LL, -33LL, -32LL, -17LL, -16LL, -1LL, 0LL, 15LL, 16LL, 63LL, 64LL})
	{
		for (int64 Y : {-33LL, -16LL, -1LL, 0LL, 16LL, 64LL})
		{
			for (int64 Z : {-64LL, -17LL, 0LL, 15LL, 64LL})
			{
				int32 Expected = 0, Actual = 123456;
				TestTrue(TEXT("scalar sample"),
					VoxelLand::M5::PocketNoise::TrySample(X, Y, Z, 16, -1, Stage, Salt, NotCancelled, Expected));
				TestEqual(TEXT("cached sample"), Cache->Sample(X, Y, Z, NotCancelled, Actual).Code,
					EVoxelLandM5ErrorCode::None);
				TestEqual(TEXT("scalar parity"), Actual, Expected);
			}
		}
	}
	int32 Sentinel = 314159;
	TestEqual(TEXT("outside rejected"), Cache->Sample(MAX_int64, 0, 0, NotCancelled, Sentinel).Code,
		EVoxelLandM5ErrorCode::OutOfDomain);
	TestEqual(TEXT("outside preserves value"), Sentinel, 314159);
	int32 Polls = 0;
	const auto CancelMiddle = [&Polls]() {
		return ++Polls == 5;
	};
	TestEqual(
		TEXT("cancellation"), Cache->Sample(1, 1, 1, CancelMiddle, Sentinel).Code, EVoxelLandM5ErrorCode::Cancelled);
	TestEqual(TEXT("cancellation preserves value"), Sentinel, 314159);

	// One coordinate creates exactly eight corners: 80 initialization work,
	// 64 lookup work and eight separately admitted 512-unit SHA misses.
	FVoxelLandM5PointCm Origin;
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> ExactBudget, ShortBudget;
	TestTrue(TEXT("exact budget"), FVoxelLandM5Budget::TryCreate(20000, 4240, ExactBudget));
	TestTrue(TEXT("short budget"), FVoxelLandM5Budget::TryCreate(20000, 4239, ShortBudget));
	TUniquePtr<FVoxelLandM5PocketNoiseCache> Exact, Short;
	TestEqual(TEXT("exact cache"),
		FVoxelLandM5PocketNoiseCache::Build(
			Origin, Origin, 16, -1, Stage, Salt, ExactBudget.ToSharedRef(), NotCancelled, Exact)
			.Code,
		EVoxelLandM5ErrorCode::None);
	TestEqual(TEXT("short cache"),
		FVoxelLandM5PocketNoiseCache::Build(
			Origin, Origin, 16, -1, Stage, Salt, ShortBudget.ToSharedRef(), NotCancelled, Short)
			.Code,
		EVoxelLandM5ErrorCode::None);
	if (!Exact || !Short)
	{
		return false;
	}
	TestEqual(
		TEXT("exact work succeeds"), Exact->Sample(0, 0, 0, NotCancelled, Sentinel).Code, EVoxelLandM5ErrorCode::None);
	TestEqual(TEXT("independent origin"), Sentinel, 24801);
	Sentinel = 314159;
	TestEqual(TEXT("one short rejects"), Short->Sample(0, 0, 0, NotCancelled, Sentinel).Code,
		EVoxelLandM5ErrorCode::BudgetExceeded);
	TestEqual(TEXT("budget preserves value"), Sentinel, 314159);
	int64 Live = 0, Peak = 0, Work = 0;
	ShortBudget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("last miss not charged"), Work, int64(3728));
	Exact.Reset();
	ExactBudget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("cache lease released"), Live, int64(0));

	// Huge sparse domains fail before allocation; caller may select scalar fallback.
	TUniquePtr<FVoxelLandM5PocketNoiseCache> Prior = MoveTemp(Cache);
	TestEqual(TEXT("tiny period bound"),
		FVoxelLandM5PocketNoiseCache::Build(Min, Max, 1, -1, Stage, Salt, Budget.ToSharedRef(), NotCancelled, Prior)
			.Code,
		EVoxelLandM5ErrorCode::BudgetExceeded);
	TestTrue(TEXT("failed build preserves prior owner"), static_cast<bool>(Prior));
	FVoxelLandM5PointCm Edge;
	Edge.X = 1048576;
	Edge.Y = -1048576;
	Edge.Z = 0;
	TUniquePtr<FVoxelLandM5PocketNoiseCache> Extreme;
	TestEqual(TEXT("extreme cache"),
		FVoxelLandM5PocketNoiseCache::Build(
			Edge, Edge, 1024, -1, Stage, Salt, Budget.ToSharedRef(), NotCancelled, Extreme)
			.Code,
		EVoxelLandM5ErrorCode::None);
	if (Extreme)
	{
		TestEqual(TEXT("extreme cached sample"), Extreme->Sample(Edge.X, Edge.Y, Edge.Z, NotCancelled, Sentinel).Code,
			EVoxelLandM5ErrorCode::None);
		TestEqual(TEXT("independent extreme"), Sentinel, -16330);
	}
	return true;
}
#endif
