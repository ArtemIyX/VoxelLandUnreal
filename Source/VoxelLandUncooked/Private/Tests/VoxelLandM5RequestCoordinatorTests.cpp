#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "World/VoxelLandM5PlanningWorker.h"
#include "World/VoxelLandM5RequestCoordinator.h"
#include "World/VoxelLandM5RequestHandle.h"

class FVoxelLandM5FakePlanningWorker final : public IVoxelLandM5PlanningWorker
{
public:
	virtual FVoxelLandM5Diagnostic Start() override
	{
		++Starts;
		if (bCloseOnStart && Owner)
		{
			Owner->Close();
		}
		return {};
	}
	virtual bool Poll(FVoxelLandM5Diagnostic& OutDiagnostic) override
	{
		if (bCloseOnPoll && Owner)
		{
			Owner->Close();
		}
		if (!bReleased)
		{
			return false;
		}
		OutDiagnostic = {};
		return true;
	}
	virtual void Cancel() override { ++Cancels; }
	virtual void Wait() override { ++Waits; }
	int32 Starts = 0;
	int32 Cancels = 0;
	int32 Waits = 0;
	bool bReleased = false;
	bool bCloseOnStart = false;
	bool bCloseOnPoll = false;
	FVoxelLandM5RequestCoordinator* Owner = nullptr;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5RequestCoordinatorTest, "VoxelLand.M5.Runtime.RequestCoalescing",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5RequestCoordinatorTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5RequestCoordinator First(2);
	FVoxelLandM5RequestCoordinator Second(1);
	const TArray<uint8> Key{1, 2};
	const TArray<uint8> OtherKey{1, 3};
	TSharedRef<FVoxelLandM5FakePlanningWorker, ESPMode::ThreadSafe> Worker =
		MakeShared<FVoxelLandM5FakePlanningWorker, ESPMode::ThreadSafe>();
	TSharedRef<FVoxelLandM5FakePlanningWorker, ESPMode::ThreadSafe> Unused =
		MakeShared<FVoxelLandM5FakePlanningWorker, ESPMode::ThreadSafe>();
	TSharedRef<FVoxelLandM5FakePlanningWorker, ESPMode::ThreadSafe> OtherWorld =
		MakeShared<FVoxelLandM5FakePlanningWorker, ESPMode::ThreadSafe>();
	FVoxelLandM5Diagnostic Diagnostic;
	TSharedPtr<FVoxelLandM5RequestHandle, ESPMode::ThreadSafe> A = First.Request(Key, Worker, Diagnostic);
	TSharedPtr<FVoxelLandM5RequestHandle, ESPMode::ThreadSafe> B = First.Request(Key, Unused, Diagnostic);
	TSharedPtr<FVoxelLandM5RequestHandle, ESPMode::ThreadSafe> C = Second.Request(Key, OtherWorld, Diagnostic);
	if (!TestTrue(TEXT("Consumers admitted"), A.IsValid() && B.IsValid() && C.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("One producer per key"), Worker->Starts, 1);
	TestEqual(TEXT("Joining does not start duplicate worker"), Unused->Starts, 0);
	TestEqual(TEXT("Independent world starts its own worker"), OtherWorld->Starts, 1);
	A->Cancel();
	First.Poll();
	TestEqual(TEXT("One cancellation leaves shared producer running"), Worker->Cancels, 0);
	TestFalse(TEXT("Remaining consumer pending at barrier"), B->TryGetResult(Diagnostic));
	Worker->bReleased = true;
	First.Poll();
	TestTrue(TEXT("Remaining consumer completed"), B->TryGetResult(Diagnostic));
	TestEqual(TEXT("Synthetic success cannot publish"), Diagnostic.Code, EVoxelLandM5ErrorCode::Unproven);
	TestTrue(TEXT("Cancelled consumer remains terminal"), A->TryGetResult(Diagnostic));
	TestEqual(TEXT("Cancelled consumer remains cancelled"), Diagnostic.Code, EVoxelLandM5ErrorCode::Cancelled);
	TestFalse(TEXT("Other world remains pending"), C->TryGetResult(Diagnostic));

	TSharedRef<FVoxelLandM5FakePlanningWorker, ESPMode::ThreadSafe> CancelWorker =
		MakeShared<FVoxelLandM5FakePlanningWorker, ESPMode::ThreadSafe>();
	TSharedPtr<FVoxelLandM5RequestHandle, ESPMode::ThreadSafe> D = First.Request(OtherKey, CancelWorker, Diagnostic);
	if (!TestTrue(TEXT("New producer admitted"), D.IsValid()))
	{
		return false;
	}
	D->Cancel();
	First.Poll();
	TestEqual(TEXT("Last cancellation reaches worker"), CancelWorker->Cancels, 1);
	TestEqual(TEXT("Worker retained until release"), CancelWorker->Waits, 0);
	CancelWorker->bReleased = true;
	First.Poll();
	TestEqual(TEXT("Worker joined after completion"), CancelWorker->Waits, 1);
	Second.Close();
	TestEqual(TEXT("Close cancels own worker"), OtherWorld->Cancels, 1);
	TestEqual(TEXT("Close joins own worker"), OtherWorld->Waits, 1);
	TestTrue(TEXT("Close finishes own consumer"), C->TryGetResult(Diagnostic));
	TestEqual(TEXT("Close reports cancellation"), Diagnostic.Code, EVoxelLandM5ErrorCode::Cancelled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5RequestReentryTest, "VoxelLand.M5.Runtime.ReentrantClose",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5RequestReentryTest::RunTest(const FString& Parameters)
{
	const TArray<uint8> Key{1};
	FVoxelLandM5Diagnostic Diagnostic;
	FVoxelLandM5RequestCoordinator StartOwner;
	TSharedRef<FVoxelLandM5FakePlanningWorker, ESPMode::ThreadSafe> StartWorker =
		MakeShared<FVoxelLandM5FakePlanningWorker, ESPMode::ThreadSafe>();
	StartWorker->Owner = &StartOwner;
	StartWorker->bCloseOnStart = true;
	TestFalse(TEXT("Close during Start rejects request"), StartOwner.Request(Key, StartWorker, Diagnostic).IsValid());
	TestEqual(TEXT("Started worker cancelled"), StartWorker->Cancels, 1);
	TestEqual(TEXT("Started worker joined"), StartWorker->Waits, 1);

	FVoxelLandM5RequestCoordinator PollOwner;
	TSharedRef<FVoxelLandM5FakePlanningWorker, ESPMode::ThreadSafe> PollWorker =
		MakeShared<FVoxelLandM5FakePlanningWorker, ESPMode::ThreadSafe>();
	PollWorker->Owner = &PollOwner;
	TSharedPtr<FVoxelLandM5RequestHandle, ESPMode::ThreadSafe> Handle = PollOwner.Request(Key, PollWorker, Diagnostic);
	if (!TestTrue(TEXT("Poll request admitted"), Handle.IsValid()))
	{
		return false;
	}
	PollWorker->bCloseOnPoll = true;
	PollOwner.Poll();
	TestEqual(TEXT("Close during Poll cancels worker"), PollWorker->Cancels, 1);
	TestEqual(TEXT("Close during Poll joins worker"), PollWorker->Waits, 1);
	TestTrue(TEXT("Close finishes consumer"), Handle->TryGetResult(Diagnostic));
	TestEqual(TEXT("Close reports cancellation"), Diagnostic.Code, EVoxelLandM5ErrorCode::Cancelled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5RequestConsumerLimitTest, "VoxelLand.M5.Runtime.ConsumerLimit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5RequestConsumerLimitTest::RunTest(const FString& Parameters)
{
	FVoxelLandM5RequestCoordinator Owner;
	TSharedRef<FVoxelLandM5FakePlanningWorker, ESPMode::ThreadSafe> Worker =
		MakeShared<FVoxelLandM5FakePlanningWorker, ESPMode::ThreadSafe>();
	TArray<uint8> Key;
	Key.SetNumZeroed(8 * 1024 * 1024 + 8192 + 64);
	TArray<TSharedPtr<FVoxelLandM5RequestHandle, ESPMode::ThreadSafe>> Handles;
	FVoxelLandM5Diagnostic Diagnostic;
	for (int32 Index = 0; Index < 256; ++Index)
	{
		Handles.Add(Owner.Request(Key, Worker, Diagnostic));
		if (!TestTrue(TEXT("Consumer admitted within bound"), Handles.Last().IsValid()))
		{
			return false;
		}
	}
	TestFalse(TEXT("Additional consumer rejected"), Owner.Request(Key, Worker, Diagnostic).IsValid());
	TestEqual(TEXT("Consumer limit diagnostic"), Diagnostic.Code, EVoxelLandM5ErrorCode::BudgetExceeded);
	Handles[0]->Cancel();
	TestTrue(TEXT("Cancelled consumer pruned"), Owner.Request(Key, Worker, Diagnostic).IsValid());
	Key.Add(0);
	TestFalse(TEXT("Oversized key rejected"), Owner.Request(Key, Worker, Diagnostic).IsValid());
	return true;
}

#endif
