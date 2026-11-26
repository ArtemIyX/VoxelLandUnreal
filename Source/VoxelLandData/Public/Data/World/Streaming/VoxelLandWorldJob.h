#pragma once

#include "CoreMinimal.h"
#include "Data/Field/Chunks/VoxelLandChunkKey.h"
#include "Data/World/Streaming/VoxelLandWorldCancel.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Data/World/Streaming/VoxelLandTestBarrier.h"
#endif
class FVoxelLandGenerator;
struct FVoxelLandMaterialAppearance;
struct FVoxelLandChunkData;

namespace VoxelLandWorldPrivate
{
struct FM7RenderRequest;

struct FJob
{
	FVoxelLandChunkKey Key;
	uint64 Epoch = 0, Ticket = 0;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
	TSharedPtr<FCancel, ESPMode::ThreadSafe> Cancel;
	TSharedPtr<const FM7RenderRequest, ESPMode::ThreadSafe> M7Visual;
	TSharedPtr<const FVoxelLandMaterialAppearance, ESPMode::ThreadSafe> Appearance;
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> ExistingData;
	bool bRender = false;
#if WITH_DEV_AUTOMATION_TESTS
	TSharedPtr<FVoxelLandTestBarrier, ESPMode::ThreadSafe> Barrier;
#endif
};
}
