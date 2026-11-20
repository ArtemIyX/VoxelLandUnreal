#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandWorldRecord.h"
#include "Data/Structs/VoxelLandWorldSharedWork.h"
#include "UObject/WeakObjectPtrTemplates.h"
class AVoxelLandRegion;
class FRunnableThread;
class FVoxelLandGenerator;
namespace VoxelLandWorldPrivate
{
class FWorker;
}

struct FVoxelLandWorldState
{
	FVoxelLandWorldState();
	~FVoxelLandWorldState();
	uint64 Epoch = 1, NextTicket = 0, Cancelled = 0, Stale = 0, Failed = 0;
	int32 Active = 0;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
	TSharedRef<VoxelLandWorldPrivate::FSharedWork, ESPMode::ThreadSafe> Work =
		MakeShared<VoxelLandWorldPrivate::FSharedWork, ESPMode::ThreadSafe>();
	TArray<TUniquePtr<VoxelLandWorldPrivate::FWorker>> Workers;
	TArray<FRunnableThread*> Threads;
	TMap<FVoxelLandChunkKey, VoxelLandWorldPrivate::FRecord> Records;
	TMap<FIntVector, TWeakObjectPtr<AVoxelLandRegion>> RegionMap;
	TSet<FIntVector> HiddenRegions;
#if WITH_DEV_AUTOMATION_TESTS
	TSharedPtr<FVoxelLandTestBarrier, ESPMode::ThreadSafe> NextBarrier;
#endif
	FIntVector CameraChunk = FIntVector::ZeroValue;
	FIntVector RegionDimensions = FIntVector(10, 10, 2);
	FVector ViewVelocity = FVector::ZeroVector;
	int32 LastHorizontal = -1, LastVertical = -1, LastLimit = -1;
	FIntVector LastVelocitySector = FIntVector::ZeroValue;
	bool bDemandComplete = false;
#if WITH_DEV_AUTOMATION_TESTS
	TOptional<FVector> TestView;
#endif
	bool bHasCamera = false, bServer = false;
};
