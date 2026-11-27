#if WITH_DEV_AUTOMATION_TESTS

#include "VoxelLandPCGWorkFence.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandPCGWorkFenceTest, "VoxelLand.PCG.WorkFence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVoxelLandPCGWorkFenceTest::RunTest(const FString&)
{
	FVoxelLandPCGWorkFence Fence;
	TestFalse(TEXT("Fresh snapshot has no workers"), Fence.HasWorkers());
	TestFalse(TEXT("Fresh snapshot accepts work"), Fence.IsCancelled());
	if (!TestTrue(TEXT("Admit first worker"), Fence.TryBeginWorker()))
	{
		return false;
	}
	if (!TestTrue(TEXT("Admit second worker"), Fence.TryBeginWorker()))
	{
		Fence.EndWorker();
		return false;
	}
	TestEqual(TEXT("Both reservations are visible"), Fence.GetWorkerCount(), 2);
	Fence.Cancel();
	TestTrue(TEXT("Retiring a snapshot closes admission"), Fence.IsCancelled());
	TestFalse(TEXT("A cancelled snapshot rejects further workers"), Fence.TryBeginWorker());
	TestEqual(TEXT("Cancellation retains both detached reservations"), Fence.GetWorkerCount(), 2);
	Fence.EndWorker();
	TestTrue(TEXT("One remaining worker keeps the snapshot charged"), Fence.HasWorkers());
	Fence.EndWorker();
	TestFalse(TEXT("The snapshot can be uncharged after the last worker releases"), Fence.HasWorkers());
	TestFalse(TEXT("Draining workers does not reopen admission"), Fence.TryBeginWorker());
	return true;
}

#endif
