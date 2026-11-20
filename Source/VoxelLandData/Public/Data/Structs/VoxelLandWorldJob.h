#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandChunkKey.h"
#include "Data/Structs/VoxelLandWorldCancel.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Structs/VoxelLandTestBarrier.h"
#endif
class FVoxelLandGenerator;

namespace VoxelLandWorldPrivate
{
struct FJob
{
	FVoxelLandChunkKey Key;
	uint64 Epoch = 0, Ticket = 0;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
	TSharedPtr<FCancel, ESPMode::ThreadSafe> Cancel;
	bool bRender = false;
#if WITH_DEV_AUTOMATION_TESTS
	TSharedPtr<FVoxelLandTestBarrier, ESPMode::ThreadSafe> Barrier;
#endif
};
}
