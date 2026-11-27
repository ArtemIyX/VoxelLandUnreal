#include "VoxelLandPCGTerrainData.h"
#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Generation/Collision/VoxelLandCollisionField.h"
#include "Generation/Placement/VoxelLandTerrainPlacementInput.h"
#include "Generation/Placement/VoxelLandTerrainPlacementPoint.h"
#include "World/VoxelLandWorld.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

PCG_DEFINE_TYPE_INFO(FPCGDataTypeInfoVoxelTerrain, UPCGVoxelTerrainData);

void UPCGVoxelTerrainData::SetMaskProgram(TSharedPtr<const FVoxelLandMaskGraphProgram, ESPMode::ThreadSafe> InProgram,
	uint64 InSeed, TSharedPtr<const IVoxelLandMaterialSignalSource, ESPMode::ThreadSafe> InTerrainSignals)
{
	check(IsInGameThread());
	MaskProgram = MoveTemp(InProgram);
	MaskSeed = InSeed;
	TerrainSignals = MoveTemp(InTerrainSignals);
}

FVoxelLandTerrainPlacementInput UPCGVoxelTerrainData::GetPlacementInput() const
{
	check(View.Field);
	return {*View.Field, Chunks, LogicalBounds};
}

#if WITH_EDITOR
void UPCGVoxelTerrainData::RecordCandidateDebug(TConstArrayView<FVoxelLandTerrainPlacementPoint> Points) const
{
	if (!bCaptureCandidates)
	{
		return;
	}
	FScopeLock Lock(&DebugCandidateLock);
	constexpr int32 MaxDebugCandidates = 64;
	for (const FVoxelLandTerrainPlacementPoint& Point : Points)
	{
		if (DebugCandidates.Num() == MaxDebugCandidates)
		{
			break;
		}
		DebugCandidates.Add(Point.Position);
	}
}

TArray<FVector> UPCGVoxelTerrainData::GetDebugCandidates() const
{
	FScopeLock Lock(&DebugCandidateLock);
	return DebugCandidates;
}
#endif

EVoxelLandPCGTerrainDataStatus UPCGVoxelTerrainData::Capture(AVoxelLandWorld& World, const FGuid& LeaseId,
	TConstArrayView<FVoxelLandChunkKey> Keys, const FBox& LogicalOwnershipBounds, UObject* Outer,
	UPCGVoxelTerrainData*& OutData, FString* OutError)
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_PCGCaptureTerrain);
	OutData = nullptr;
	if (OutError)
	{
		OutError->Reset();
	}
	if (!LeaseId.IsValid() || !LogicalOwnershipBounds.IsValid || LogicalOwnershipBounds.Min.ContainsNaN()
		|| LogicalOwnershipBounds.Max.ContainsNaN() || !IsValid(Outer))
	{
		return EVoxelLandPCGTerrainDataStatus::Unavailable;
	}
	if (Keys.IsEmpty())
	{
		return EVoxelLandPCGTerrainDataStatus::Empty;
	}
	FVoxelLandCollisionWorldView Captured;
	if (!World.CaptureCollisionView(LeaseId, Captured) || !IsValid(World.GetWorld()))
	{
		return EVoxelLandPCGTerrainDataStatus::Unavailable;
	}
	if (!Captured.Field || !Captured.Field->IsValid())
	{
		return EVoxelLandPCGTerrainDataStatus::Failed;
	}
	TArray<FChunkPtr> ReadyChunks;
	ReadyChunks.Reserve(Keys.Num());
	bool bMissing = false;
	for (const FVoxelLandChunkKey& Key : Keys)
	{
		if (Key.LOD != 0)
		{
			return EVoxelLandPCGTerrainDataStatus::Unavailable;
		}
		FChunkPtr Chunk = World.FindChunk(Key);
		if (Chunk)
		{
			ReadyChunks.Add(MoveTemp(Chunk));
			continue;
		}
		FString Error;
		if (World.GetTerminalTerrainPlacementError(Key.Coordinate, Error))
		{
			if (OutError)
			{
				*OutError = MoveTemp(Error);
			}
			return EVoxelLandPCGTerrainDataStatus::Failed;
		}
		bMissing = true;
	}
	if (bMissing)
	{
		return EVoxelLandPCGTerrainDataStatus::Pending;
	}
	ReadyChunks.Sort([](const FChunkPtr& A, const FChunkPtr& B) {
		const FIntVector& KeyA = A->Key.Coordinate;
		const FIntVector& KeyB = B->Key.Coordinate;
		if (KeyA.Z != KeyB.Z)
		{
			return KeyA.Z < KeyB.Z;
		}
		return KeyA.Y != KeyB.Y ? KeyA.Y < KeyB.Y : KeyA.X < KeyB.X;
	});
	UPCGVoxelTerrainData* Data = NewObject<UPCGVoxelTerrainData>(Outer, NAME_None, RF_Transient);
	Data->LogicalBounds = LogicalOwnershipBounds;
	Data->View = MoveTemp(Captured);
	Data->Chunks = MoveTemp(ReadyChunks);
	OutData = Data;
	return EVoxelLandPCGTerrainDataStatus::Ready;
}
