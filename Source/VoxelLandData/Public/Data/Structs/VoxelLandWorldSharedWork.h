#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Data/Structs/VoxelLandWorldJob.h"
#include "Data/Structs/VoxelLandWorldCompletion.h"

namespace VoxelLandWorldPrivate
{
struct FSharedWork
{
	FCriticalSection Mutex;
	TQueue<FJob> Jobs;
	TQueue<FCompletion, EQueueMode::Mpsc> Completed;
	FEvent* Wake = FPlatformProcess::GetSynchEventFromPool(false);
	TAtomic<bool> bStop{false};
	~FSharedWork() { FPlatformProcess::ReturnSynchEventToPool(Wake); }
};
}
