#pragma once
#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/PlatformProcess.h"

// Holds an actual worker at a deterministic checkpoint until cancellation or timeout.
struct FVoxelLandReloadTestBarrier
{
	TAtomic<bool> Started{false};
	TAtomic<bool> SawCancellation{false};
	TAtomic<bool> TimedOut{false};
	void WaitForCancellation(const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe>& Cancel)
	{
		Started.Store(true);
		const double Deadline = FPlatformTime::Seconds() + 10.0;
		while (!Cancel->Load() && FPlatformTime::Seconds() < Deadline)
		{
			FPlatformProcess::Sleep(0.001f);
		}
		SawCancellation.Store(Cancel->Load());
		TimedOut.Store(!Cancel->Load());
	}
};
#endif
