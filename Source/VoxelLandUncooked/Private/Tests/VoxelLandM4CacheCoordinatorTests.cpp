#if WITH_DEV_AUTOMATION_TESTS

#include "Generation/VoxelLandM4GridLease.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandM4PrepassCoordinator.h"

namespace
{
constexpr EAutomationTestFlags M4CoordinatorFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

class FM4CoordinatorTestLease final : public FVoxelLandM4GridLease
{
public:
	explicit FM4CoordinatorTestLease(FThreadSafeCounter& InReleases)
		: Releases(InReleases)
	{
	}
	~FM4CoordinatorTestLease() override { Releases.Increment(); }

private:
	FThreadSafeCounter& Releases;
};

bool M4CoordinatorWait(const TSharedPtr<FM4PrepassSubscription, ESPMode::ThreadSafe>& Subscription)
{
	const double Deadline = FPlatformTime::Seconds() + 5.0;
	while (Subscription && !Subscription->IsFinished() && FPlatformTime::Seconds() < Deadline)
	{
		FPlatformProcess::Sleep(0.001f);
	}
	return Subscription && Subscription->IsFinished();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4CoordinatorSharingTest, "VoxelLand.M4.CacheCoordinator.Sharing", M4CoordinatorFlags)
bool FVoxelLandM4CoordinatorSharingTest::RunTest(const FString& Parameters)
{
	FThreadSafeCounter Reservations, Releases, ProducerRuns, ProducerCancelled;
	FEvent* Started = FPlatformProcess::GetSynchEventFromPool(true);
	FEvent* Proceed = FPlatformProcess::GetSynchEventFromPool(true);
	FM4PrepassCoordinator Coordinator(2, 1);
	auto Reserve = [&]() -> FM4PrepassCoordinator::FLease {
		Reservations.Increment();
		return MakeShared<FM4CoordinatorTestLease, ESPMode::ThreadSafe>(Releases);
	};
	FM4PrepassCoordinator::FProducer Producer = [&](const FM4PrepassCoordinator::FLease& Lease,
													TFunctionRef<bool()> Cancelled, FM4PrepassCoordinator::FGrid& Grid,
													FString&) {
		ProducerRuns.Increment();
		Started->Trigger();
		Proceed->Wait(5000);
		if (Cancelled())
		{
			ProducerCancelled.Increment();
			return false;
		}
		const TArray<int32> Heights = {1, 2, 3, 4};
		return FVoxelLandM4HeightGrid::Create(0, 0, 1, 1, 10, Heights, Grid, Cancelled, Lease);
	};
	const TArray<uint8> Key = {1, 2, 3};
	FString Error;
	auto First = Coordinator.Subscribe(Key, Reserve, MoveTemp(Producer), Error);
	const bool bStarted = Started->Wait(5000);
	TestTrue(TEXT("producer starts"), bStarted);
	FM4PrepassCoordinator::FProducer ShouldNotRun = [&](const FM4PrepassCoordinator::FLease&, TFunctionRef<bool()>,
														FM4PrepassCoordinator::FGrid&, FString&) {
		ProducerRuns.Increment();
		return false;
	};
	auto Second = Coordinator.Subscribe(Key, Reserve, MoveTemp(ShouldNotRun), Error);
	TestTrue(TEXT("matching requests share one admission"),
		First.IsValid() && Second.IsValid() && Reservations.GetValue() == 1);
	if (First)
	{
		First->Cancel();
	}
	Proceed->Trigger();
	TestTrue(TEXT("remaining subscriber finishes"), M4CoordinatorWait(Second));
	FM4PrepassCoordinator::FGrid Grid;
	if (Second)
	{
		TestTrue(TEXT("remaining subscriber receives grid"), Second->ReadResult(Grid, Error));
	}
	TestEqual(TEXT("single producer ran"), ProducerRuns.GetValue(), 1);
	TestEqual(TEXT("one cancellation did not cancel producer"), ProducerCancelled.GetValue(), 0);
	FM4PrepassCoordinator::FProducer CachedShouldNotRun =
		[&](const FM4PrepassCoordinator::FLease&, TFunctionRef<bool()>, FM4PrepassCoordinator::FGrid&, FString&) {
			ProducerRuns.Increment();
			return false;
		};
	auto Cached = Coordinator.Subscribe(Key, Reserve, MoveTemp(CachedShouldNotRun), Error);
	TestTrue(TEXT("completed result reused without reservation"), Cached.IsValid() && Reservations.GetValue() == 1);
	Coordinator.Shutdown();
	if (Second)
	{
		Second->Cancel();
	}
	if (Cached)
	{
		Cached->Cancel();
	}
	TestEqual(TEXT("reader keeps reservation after coordinator shutdown"), Releases.GetValue(), 0);
	Grid.Reset();
	TestEqual(TEXT("detached handles do not retain the last reservation"), Releases.GetValue(), 1);
	FPlatformProcess::ReturnSynchEventToPool(Started);
	FPlatformProcess::ReturnSynchEventToPool(Proceed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4CoordinatorCancellationTest, "VoxelLand.M4.CacheCoordinator.Cancellation", M4CoordinatorFlags)
bool FVoxelLandM4CoordinatorCancellationTest::RunTest(const FString& Parameters)
{
	FThreadSafeCounter Releases, SawCancellation;
	FEvent* Started = FPlatformProcess::GetSynchEventFromPool(true);
	FM4PrepassCoordinator Coordinator(1, 0);
	auto Reserve = [&]() -> FM4PrepassCoordinator::FLease {
		return MakeShared<FM4CoordinatorTestLease, ESPMode::ThreadSafe>(Releases);
	};
	FM4PrepassCoordinator::FProducer Producer = [&](const FM4PrepassCoordinator::FLease&,
													TFunctionRef<bool()> Cancelled, FM4PrepassCoordinator::FGrid&,
													FString&) {
		Started->Trigger();
		const double Deadline = FPlatformTime::Seconds() + 5.0;
		while (!Cancelled() && FPlatformTime::Seconds() < Deadline)
		{
			FPlatformProcess::Sleep(0.001f);
		}
		if (Cancelled())
		{
			SawCancellation.Increment();
		}
		return false;
	};
	const TArray<uint8> Key = {7};
	FString Error;
	auto Subscription = Coordinator.Subscribe(Key, Reserve, MoveTemp(Producer), Error);
	TestTrue(TEXT("producer starts"), Started->Wait(5000));
	if (Subscription)
	{
		Subscription->Cancel();
	}
	Coordinator.Shutdown();
	TestEqual(TEXT("last consumer requests cancellation"), SawCancellation.GetValue(), 1);
	TestEqual(TEXT("failed producer releases reservation after join"), Releases.GetValue(), 1);
	FM4PrepassCoordinator::FProducer Rejected = [](const FM4PrepassCoordinator::FLease&, TFunctionRef<bool()>,
													FM4PrepassCoordinator::FGrid&, FString&) {
		return false;
	};
	TestFalse(
		TEXT("shutdown rejects new work"), Coordinator.Subscribe(Key, Reserve, MoveTemp(Rejected), Error).IsValid());
	Subscription.Reset();
	FPlatformProcess::ReturnSynchEventToPool(Started);
	return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4CoordinatorLeaseGuardTest, "VoxelLand.M4.CacheCoordinator.LeaseGuard", M4CoordinatorFlags)
bool FVoxelLandM4CoordinatorLeaseGuardTest::RunTest(const FString& Parameters)
{
	FThreadSafeCounter Releases;
	FM4PrepassCoordinator Coordinator(1, 1);
	auto Reserve = [&]() -> FM4PrepassCoordinator::FLease {
		return MakeShared<FM4CoordinatorTestLease, ESPMode::ThreadSafe>(Releases);
	};
	FM4PrepassCoordinator::FProducer Producer = [](const FM4PrepassCoordinator::FLease&, TFunctionRef<bool()>,
													FM4PrepassCoordinator::FGrid& Grid, FString&) {
		const TArray<int32> Heights = {0, 0, 0, 0};
		// Deliberately omit the supplied lease: the coordinator must reject it.
		return FVoxelLandM4HeightGrid::Create(0, 0, 1, 1, 1, Heights, Grid);
	};
	const TArray<uint8> Key = {9};
	FString Error;
	auto Subscription = Coordinator.Subscribe(Key, Reserve, MoveTemp(Producer), Error);
	TestTrue(TEXT("bad producer finishes"), M4CoordinatorWait(Subscription));
	FM4PrepassCoordinator::FGrid Output;
	if (Subscription)
	{
		TestFalse(TEXT("grid without reservation lease is rejected"), Subscription->ReadResult(Output, Error));
	}
	TestFalse(TEXT("invalid grid not published"), Output.IsValid());
	Coordinator.Shutdown();
	TestEqual(TEXT("reservation released after rejected producer finishes"), Releases.GetValue(), 1);
	return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4CoordinatorExactKeyTest, "VoxelLand.M4.CacheCoordinator.ExactKey", M4CoordinatorFlags)
bool FVoxelLandM4CoordinatorExactKeyTest::RunTest(const FString& Parameters)
{
	FThreadSafeCounter Reservations;
	FThreadSafeCounter Releases;
	FEvent* Proceed = FPlatformProcess::GetSynchEventFromPool(true);
	FM4PrepassCoordinator Coordinator(2, 0);
	auto Reserve = [&]() -> FM4PrepassCoordinator::FLease {
		Reservations.Increment();
		return MakeShared<FM4CoordinatorTestLease, ESPMode::ThreadSafe>(Releases);
	};
	auto MakeProducer = [&]() -> FM4PrepassCoordinator::FProducer {
		return [Proceed](const FM4PrepassCoordinator::FLease& Lease, TFunctionRef<bool()> Cancelled,
				   FM4PrepassCoordinator::FGrid& Grid, FString&) {
			Proceed->Wait(5000);
			const TArray<int32> Values = {0, 1, 2, 3};
			return !Cancelled() && FVoxelLandM4HeightGrid::Create(0, 0, 1, 1, 1, Values, Grid, Cancelled, Lease);
		};
	};
	const TArray<uint8> FirstKey = {1, 2, 3, 4};
	const TArray<uint8> SecondKey = {1, 2, 3, 5};
	FString Error;
	auto First = Coordinator.Subscribe(FirstKey, Reserve, MakeProducer(), Error);
	auto Second = Coordinator.Subscribe(SecondKey, Reserve, MakeProducer(), Error);
	TestTrue(TEXT("distinct byte identities obtain distinct producers"), First.IsValid() && Second.IsValid());
	TestEqual(TEXT("distinct identities each reserve"), Reservations.GetValue(), 2);
	Proceed->Trigger();
	TestTrue(TEXT("first finishes"), M4CoordinatorWait(First));
	TestTrue(TEXT("second finishes"), M4CoordinatorWait(Second));
	Coordinator.Shutdown();
	First.Reset();
	Second.Reset();
	TestEqual(TEXT("both leases released"), Releases.GetValue(), 2);
	FPlatformProcess::ReturnSynchEventToPool(Proceed);
	return true;
}
#endif
