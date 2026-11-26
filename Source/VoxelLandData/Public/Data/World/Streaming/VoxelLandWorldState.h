#pragma once

#include "CoreMinimal.h"
#include "Data/World/Streaming/VoxelLandWorldRecord.h"
#include "Data/World/Streaming/VoxelLandWorldSharedWork.h"
#include "UObject/WeakObjectPtrTemplates.h"
class AVoxelLandRegion;
class FRunnableThread;
class FVoxelLandGenerator;
struct FVoxelLandMaterialAppearance;
namespace VoxelLandWorldPrivate
{
class FWorker;
struct FM7VisualState;
}

struct FVoxelLandWorldState
{
	static constexpr int32 MaxRecords = 8192;

	FVoxelLandWorldState();
	~FVoxelLandWorldState();
	uint64 Epoch = 1, NextTicket = 0, Cancelled = 0, Stale = 0, Failed = 0;
	int32 Active = 0;
	int32 CollisionDispatchBurst = 0;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
	TArray<uint8> M6Identity;
	TSharedPtr<const FVoxelLandMaterialAppearance, ESPMode::ThreadSafe> Appearance;
	TSharedRef<VoxelLandWorldPrivate::FSharedWork, ESPMode::ThreadSafe> Work =
		MakeShared<VoxelLandWorldPrivate::FSharedWork, ESPMode::ThreadSafe>();
	TArray<TUniquePtr<VoxelLandWorldPrivate::FWorker>> Workers;
	TArray<FRunnableThread*> Threads;
	TMap<FVoxelLandChunkKey, VoxelLandWorldPrivate::FRecord> Records;
	TMap<FGuid, TSet<FVoxelLandChunkKey>> CollisionLeases;
	TMap<FIntVector, TWeakObjectPtr<AVoxelLandRegion>> RegionMap;
	TSet<FIntVector> HiddenRegions;
	TUniquePtr<VoxelLandWorldPrivate::FM7VisualState> M7Visual;
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
