#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5BudgetReservationTest, "VoxelLand.M5.Budget.Reservations",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5BudgetReservationTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("Create budget"), FVoxelLandM5Budget::TryCreate(100, 20, Budget)))
	{
		return false;
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> A;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> B;
	TestTrue(TEXT("Reserve first allocation"), Budget->TryReserveBytes(40, A));
	TestTrue(TEXT("Reserve second allocation"), Budget->TryReserveBytes(60, B));
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> SharedA = A;
	int64 Live = -1;
	int64 Peak = -1;
	int64 Work = -1;
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("Both allocations count"), Live, int64(100));
	TestEqual(TEXT("Peak includes overlapping allocations"), Peak, int64(100));

	const FVoxelLandM5BudgetLease* OriginalB = B.Get();
	TestFalse(TEXT("Over budget is rejected"), Budget->TryReserveBytes(1, B));
	TestTrue(TEXT("Failed reservation retains caller lease"), B.Get() == OriginalB);
	TestFalse(TEXT("Negative reservation is rejected"), Budget->TryReserveBytes(-1, B));
	TestFalse(TEXT("Zero reservation is rejected"), Budget->TryReserveBytes(0, B));
	TestTrue(TEXT("Invalid reservations retain lease"), B.Get() == OriginalB);
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("Failures leave live unchanged"), Live, int64(100));
	TestEqual(TEXT("Failures leave peak unchanged"), Peak, int64(100));

	A.Reset();
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("Another reference keeps reservation"), Live, int64(100));
	SharedA.Reset();
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("Last reference releases reservation"), Live, int64(60));
	TestTrue(TEXT("Replacement admitted before prior lease release"), Budget->TryReserveBytes(40, B));
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("Replacement releases prior lease"), Live, int64(40));
	TestEqual(TEXT("Peak is retained"), Peak, int64(100));
	B.Reset();
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("All reservations released"), Live, int64(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5BudgetLeaseLimitTest, "VoxelLand.M5.Budget.LeaseLimit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5BudgetLeaseLimitTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Zero;
	if (!TestTrue(TEXT("Zero limits are valid"), FVoxelLandM5Budget::TryCreate(0, 0, Zero)))
	{
		return false;
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Unchanged;
	for (int32 Index = 0; Index < FVoxelLandM5Budget::MaximumLiveLeases + 1; ++Index)
	{
		if (Zero->TryReserveBytes(0, Unchanged) || Zero->TryReserveBytes(1, Unchanged))
		{
			AddError(TEXT("Zero-cap budget admitted a reservation"));
			return false;
		}
	}
	TestFalse(TEXT("Rejected reservations do not create leases"), Unchanged.IsValid());

	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("Create high byte-cap budget"),
			FVoxelLandM5Budget::TryCreate(FVoxelLandM5Budget::MaximumLiveLeases + 1, 0, Budget)))
	{
		return false;
	}
	TArray<TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>> Leases;
	Leases.SetNum(FVoxelLandM5Budget::MaximumLiveLeases);
	for (auto& Lease : Leases)
	{
		if (!Budget->TryReserveBytes(1, Lease))
		{
			AddError(TEXT("Failed before reaching lease limit"));
			return false;
		}
	}
	TestFalse(TEXT("Bookkeeping cap rejects even with byte headroom"), Budget->TryReserveBytes(1, Unchanged));
	Leases[0].Reset();
	TestTrue(TEXT("Released lease restores one slot"), Budget->TryReserveBytes(1, Unchanged));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5BudgetConcurrentTest, "VoxelLand.M5.Budget.ConcurrentAdmission",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5BudgetConcurrentTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("Create competing budget"), FVoxelLandM5Budget::TryCreate(1, 0, Budget)))
	{
		return false;
	}
	FEvent* ReadyA = FPlatformProcess::GetSynchEventFromPool(true);
	FEvent* ReadyB = FPlatformProcess::GetSynchEventFromPool(true);
	FEvent* Proceed = FPlatformProcess::GetSynchEventFromPool(true);
	const auto Compete = [Budget, Proceed](
							 FEvent* Ready, TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>& Out) {
		Ready->Trigger();
		Proceed->Wait();
		Budget->TryReserveBytes(1, Out);
	};
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> LeaseA;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> LeaseB;
	TFuture<void> A = Async(EAsyncExecution::Thread, [Compete, ReadyA, &LeaseA]() { Compete(ReadyA, LeaseA); });
	TFuture<void> B = Async(EAsyncExecution::Thread, [Compete, ReadyB, &LeaseB]() { Compete(ReadyB, LeaseB); });
	ReadyA->Wait();
	ReadyB->Wait();
	Proceed->Trigger();
	A.Get();
	B.Get();
	TestTrue(TEXT("Exactly one overlapping reservation succeeds"), LeaseA.IsValid() != LeaseB.IsValid());
	int64 Live = 0;
	int64 Peak = 0;
	int64 Work = 0;
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("Only one byte admitted"), Live, int64(1));
	LeaseA.Reset();
	LeaseB.Reset();
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("Last lease releases charge"), Live, int64(0));
	FPlatformProcess::ReturnSynchEventToPool(ReadyA);
	FPlatformProcess::ReturnSynchEventToPool(ReadyB);
	FPlatformProcess::ReturnSynchEventToPool(Proceed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM5BudgetWorkTest, "VoxelLand.M5.Budget.WorkAndLimits",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandM5BudgetWorkTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget;
	if (!TestTrue(TEXT("Create maximum budget"), FVoxelLandM5Budget::TryCreate(MAX_int64, MAX_int64, Budget)))
	{
		return false;
	}
	const FVoxelLandM5Budget* Original = Budget.Get();
	TestFalse(TEXT("Negative byte limit rejected"), FVoxelLandM5Budget::TryCreate(-1, 0, Budget));
	TestFalse(TEXT("Negative work limit rejected"), FVoxelLandM5Budget::TryCreate(0, -1, Budget));
	TestTrue(TEXT("Invalid creation retains existing budget"), Budget.Get() == Original);

	TestTrue(TEXT("Charge near max"), Budget->TryChargeWork(MAX_int64 - 1));
	TestTrue(TEXT("Charge to max"), Budget->TryChargeWork(1));
	TestFalse(TEXT("Overflowing charge rejected"), Budget->TryChargeWork(1));
	TestFalse(TEXT("Negative charge rejected"), Budget->TryChargeWork(-1));
	TestTrue(TEXT("Zero charge at max succeeds"), Budget->TryChargeWork(0));
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	TestTrue(TEXT("Reserve max"), Budget->TryReserveBytes(MAX_int64, Lease));
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Other;
	TestFalse(TEXT("Byte overflow rejected"), Budget->TryReserveBytes(1, Other));
	TestFalse(TEXT("Failed output remains empty"), Other.IsValid());
	int64 Live = 0;
	int64 Peak = 0;
	int64 Work = 0;
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("Max live"), Live, MAX_int64);
	TestEqual(TEXT("Max peak"), Peak, MAX_int64);
	TestEqual(TEXT("Work remains charged"), Work, MAX_int64);

	TSharedPtr<FVoxelLandM5Budget, ESPMode::ThreadSafe> OtherBudget;
	TestTrue(TEXT("Create independent budget"), FVoxelLandM5Budget::TryCreate(1, 1, OtherBudget));
	TestTrue(TEXT("Another build can reserve"), OtherBudget->TryReserveBytes(1, Other));
	TestTrue(TEXT("Another build can charge work"), OtherBudget->TryChargeWork(1));
	Lease.Reset();
	Budget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("First release does not reset work"), Work, MAX_int64);
	OtherBudget->GetUsage(Live, Peak, Work);
	TestEqual(TEXT("Other build remains charged"), Live, int64(1));
	TestEqual(TEXT("Other build work remains charged"), Work, int64(1));
	return true;
}
#endif
