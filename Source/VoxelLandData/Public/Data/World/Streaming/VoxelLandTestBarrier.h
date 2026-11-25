#pragma once
#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"

// Test-only synchronization: a worker signals once its result is ready, and
// waits without touching UObjects until released or world shutdown begins.
struct FVoxelLandTestBarrier
{
	FEvent* Reached = FPlatformProcess::GetSynchEventFromPool(true);
	FEvent* Release = FPlatformProcess::GetSynchEventFromPool(true);
	bool bMarkHash = false;
	uint64 MarkerHash = 0;
	~FVoxelLandTestBarrier()
	{
		FPlatformProcess::ReturnSynchEventToPool(Reached);
		FPlatformProcess::ReturnSynchEventToPool(Release);
	}
};
#endif
