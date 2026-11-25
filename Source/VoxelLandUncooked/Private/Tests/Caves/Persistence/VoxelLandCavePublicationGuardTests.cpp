#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Data/Caves/Planning/VoxelLandCavePlan.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "World/Caves/Persistence/VoxelLandCavePublicationGuard.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5PublicationGuardTest, "VoxelLand.M5.Lifecycle.PublicationGuard",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5PublicationGuardTest::RunTest(const FString& Parameters)
{
	using FGuard = FVoxelLandM5PublicationGuard;
	FGuard First(2, 8);
	FGuard Second(1, 8);
	const TArray<uint8> KeyA{1, 2};
	const TArray<uint8> KeyB{1, 3};
	const TArray<uint8> Empty;
	FGuard::FLease A;
	FGuard::FLease B;
	FGuard::FLease Other;
	FGuard::FPlan PlanA = MakeShared<FVoxelLandM5CavePlan, ESPMode::ThreadSafe>();
	FGuard::FPlan PlanB = MakeShared<FVoxelLandM5CavePlan, ESPMode::ThreadSafe>();

	TestFalse(TEXT("Empty key is rejected"), First.TryBegin(Empty, A));
	TestTrue(TEXT("First request admitted"), First.TryBegin(KeyA, A));
	TestTrue(TEXT("Independent world admitted"), Second.TryBegin(KeyA, Other));
	TestFalse(TEXT("Foreign world's token rejected"), First.TryPublish(Other, KeyA, PlanA));
	TestFalse(TEXT("Different key rejected"), First.TryPublish(A, KeyB, PlanA));
	TestTrue(TEXT("Current request published"), First.TryPublish(A, KeyA, PlanA));
	TestFalse(TEXT("Duplicate completion rejected"), First.TryPublish(A, KeyA, PlanB));
	TestTrue(TEXT("Duplicate completion did not replace plan"), First.GetPublished() == PlanA);
	TestTrue(TEXT("Replacement admitted"), First.TryBegin(KeyB, B));
	TestFalse(TEXT("Previous publication no longer current"), First.GetPublished().IsValid());
	TestFalse(TEXT("Stale completion rejected"), First.TryPublish(A, KeyA, PlanA));
	FGuard::FLease Preserved = A;
	TestFalse(TEXT("In-flight budget rejects new request"), First.TryBegin(KeyA, Preserved));
	TestTrue(TEXT("Failed admission preserves output lease"), Preserved == A);
	First.Cancel();
	TestFalse(TEXT("Cancel invalidates replacement"), First.TryPublish(B, KeyB, PlanB));
	TestEqual(TEXT("Cancellation does not release active leases"), First.GetInFlight(), 2);
	TestTrue(TEXT("Other world is unaffected"), Second.TryPublish(Other, KeyA, PlanA));
	First.Close();
	TestFalse(TEXT("Closed guard rejects new work"), First.TryBegin(KeyA, B));
	TestTrue(TEXT("Other world's published result remains"), Second.GetPublished() == PlanA);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5PublicationLeaseTest, "VoxelLand.M5.Lifecycle.ProducerLease",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5PublicationLeaseTest::RunTest(const FString& Parameters)
{
	using FGuard = FVoxelLandM5PublicationGuard;
	FGuard Guard(1, 8);
	const TArray<uint8> Key{9};
	FGuard::FLease Lease;
	if (!TestTrue(TEXT("Request admitted"), Guard.TryBegin(Key, Lease)))
	{
		return false;
	}
	FEvent* Started = FPlatformProcess::GetSynchEventFromPool(true);
	FEvent* Proceed = FPlatformProcess::GetSynchEventFromPool(true);
	FEvent* Released = FPlatformProcess::GetSynchEventFromPool(true);
	TFuture<void> Worker = Async(EAsyncExecution::Thread, [WorkerLease = Lease, Started, Proceed, Released]() mutable {
		Started->Trigger();
		Proceed->Wait();
		WorkerLease.Reset();
		Released->Trigger();
	});
	const bool bStarted = Started->Wait(5000);
	TestTrue(TEXT("Worker reached the barrier"), bStarted);
	Guard.Cancel();
	Lease.Reset();
	FGuard::FLease Next;
	TestFalse(TEXT("Worker retains admission after consumer cancellation"), Guard.TryBegin(Key, Next));
	Proceed->Trigger();
	const bool bReleased = Released->Wait(5000);
	TestTrue(TEXT("Worker released lease"), bReleased);
	Worker.Wait();
	if (bReleased)
	{
		TestTrue(TEXT("Admission returns after producer releases its lease"), Guard.TryBegin(Key, Next));
	}
	FPlatformProcess::ReturnSynchEventToPool(Started);
	FPlatformProcess::ReturnSynchEventToPool(Proceed);
	FPlatformProcess::ReturnSynchEventToPool(Released);
	return bStarted && bReleased;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5PublicationGuardEpochTest, "VoxelLand.M5.Lifecycle.EpochExhaustion",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5PublicationGuardEpochTest::RunTest(const FString& Parameters)
{
	using FGuard = FVoxelLandM5PublicationGuard;
	FGuard Guard(2, 8);
	const TArray<uint8> Key{1};
	FGuard::FLease Lease;
	TestTrue(TEXT("Initial request admitted"), Guard.TryBegin(Key, Lease));
	FGuard::FPlan Plan = MakeShared<FVoxelLandM5CavePlan, ESPMode::ThreadSafe>();
	TestTrue(TEXT("Initial result published"), Guard.TryPublish(Lease, Key, Plan));
	Guard.SetEpochForTesting(MAX_uint64);
	FGuard::FLease Out = Lease;
	TestFalse(TEXT("Epoch exhaustion rejects admission"), Guard.TryBegin(Key, Out));
	TestTrue(TEXT("Output lease is unchanged"), Out == Lease);
	TestFalse(TEXT("Publication is cleared on exhaustion"), Guard.GetPublished().IsValid());
	Lease.Reset();
	Out.Reset();
	TestFalse(TEXT("Exhausted guard stays closed"), Guard.TryBegin(Key, Out));
	return true;
}

#endif
