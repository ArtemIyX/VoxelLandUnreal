#include "World/VoxelLandWorld.h"

#include "../VoxelLandWorldCoordinates.h"
#include "Data/Meshing/VoxelLandMeshData.h"
#include "Data/World/Streaming/VoxelLandWorldState.h"
#include "Generation/Collision/VoxelLandCollisionField.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "World/Collision/VoxelLandCollisionWorldView.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace VoxelLandWorldCollision
{
constexpr int32 MaxConsumers = 16;
constexpr int32 MaxKeysPerConsumer = 64;
constexpr int32 MaxLeasedKeys = 512;

bool ContainsKey(const TSet<FVoxelLandChunkKey>* Keys, const FVoxelLandChunkKey& Key)
{
	return Keys && Keys->Contains(Key);
}

int64 DistanceTo(const FIntVector& A, const FIntVector& B)
{
	return FMath::Abs(int64(A.X) - B.X) + FMath::Abs(int64(A.Y) - B.Y) + FMath::Abs(int64(A.Z) - B.Z);
}

void SortEvictionKeys(TArray<FVoxelLandChunkKey>& Keys, const FIntVector& Camera)
{
	Keys.Sort([Camera](const FVoxelLandChunkKey& A, const FVoxelLandChunkKey& B) {
		const int64 DistanceA = DistanceTo(A.Coordinate, Camera);
		const int64 DistanceB = DistanceTo(B.Coordinate, Camera);
		if (DistanceA != DistanceB)
		{
			return DistanceA > DistanceB;
		}
		return A.Coordinate.X != B.Coordinate.X ? A.Coordinate.X < B.Coordinate.X
			 : A.Coordinate.Y != B.Coordinate.Y ? A.Coordinate.Y < B.Coordinate.Y
												: A.Coordinate.Z < B.Coordinate.Z;
	});
}
}

bool AVoxelLandWorld::CanRequestChunk(const FVoxelLandChunkKey& Key, bool bLogRejection) const
{
	if (!State || Key.LOD != 0 || !State->Generator->IncludesChunk(Key)
		|| !VoxelLandMeshCoordinates::IsKeyInPrecisionRange(
			Key.Coordinate, State->Generator->GetProfile().VoxelSizeCm, State->Generator->IsM4Unlimited()))
	{
		return false;
	}
	if (const FVoxelLandError DemandError = State->Generator->ValidateM4ChunkDemand(Key))
	{
		if (bLogRejection)
		{
			UE_LOG(LogTemp, Warning, TEXT("VoxelLand rejected chunk (%d,%d,%d): %s"), Key.Coordinate.X,
				Key.Coordinate.Y, Key.Coordinate.Z, *DemandError.Message);
		}
		return false;
	}
	return true;
}

bool AVoxelLandWorld::UpdateCollisionDemand(const FGuid& ConsumerId, TConstArrayView<FVoxelLandChunkKey> Keys)
{
	using namespace VoxelLandWorldCollision;
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_UpdateCollisionDemand);
	if (!State || !ConsumerId.IsValid() || Keys.Num() > MaxKeysPerConsumer)
	{
		return false;
	}
	const TSet<FVoxelLandChunkKey>* Previous = State->CollisionLeases.Find(ConsumerId);
	if (!Previous && State->CollisionLeases.Num() >= MaxConsumers)
	{
		return false;
	}
	TSet<FVoxelLandChunkKey> Desired;
	Desired.Reserve(Keys.Num());
	int32 AddedRecords = 0;
	bool bChanged = Previous == nullptr;
	for (const FVoxelLandChunkKey& Key : Keys)
	{
		if (Desired.Contains(Key))
		{
			continue;
		}
		Desired.Add(Key);
		if (!ContainsKey(Previous, Key))
		{
			if (!CanRequestChunk(Key))
			{
				return false;
			}
			AddedRecords += State->Records.Contains(Key) ? 0 : 1;
			bChanged = true;
		}
	}
	if (!bChanged && Previous->Num() == Desired.Num())
	{
		return true;
	}

	const int32 Limit = FMath::Clamp(MaxResidentChunks, 1, FVoxelLandWorldState::MaxRecords);
	const int32 EvictionCount = FMath::Max(0, State->Records.Num() + AddedRecords - Limit);
	TArray<FVoxelLandChunkKey> Evictions;
	int32 LeasedKeys = AddedRecords;
	for (const TPair<FVoxelLandChunkKey, VoxelLandWorldPrivate::FRecord>& Entry : State->Records)
	{
		const int32 OtherOwners = Entry.Value.CollisionOwners - int32(ContainsKey(Previous, Entry.Key));
		const bool bDesired = Desired.Contains(Entry.Key);
		if (OtherOwners > 0 || bDesired)
		{
			++LeasedKeys;
		}
		else if (EvictionCount > 0 && !Entry.Value.bExplicit)
		{
			Evictions.Add(Entry.Key);
		}
	}
	if (LeasedKeys > MaxLeasedKeys || EvictionCount > Evictions.Num())
	{
		return false;
	}

	if (EvictionCount > 0)
	{
		SortEvictionKeys(Evictions, State->CameraChunk);
	}
	const uint64 Epoch = ConfigurationEpoch;
	ReleaseCollisionDemand(ConsumerId);
	for (int32 Index = 0; Index < EvictionCount; ++Index)
	{
		RetireChunk(Evictions[Index]);
		if (!State || ConfigurationEpoch != Epoch || State->CollisionLeases.Contains(ConsumerId))
		{
			return false;
		}
	}
	for (const FVoxelLandChunkKey& Key : Desired)
	{
		VoxelLandWorldPrivate::FRecord& Record = State->Records.FindOrAdd(Key);
		++Record.CollisionOwners;
		if (!Record.Data && !Record.bRunning && !Record.bFailed)
		{
			Record.bQueued = true;
		}
	}
	State->CollisionLeases.Add(ConsumerId, MoveTemp(Desired));
	State->bDemandComplete = false;
	return true;
}

bool AVoxelLandWorld::UpdateCollisionDemandForBounds(const FGuid& ConsumerId, const FBox& LocalBounds)
{
	using namespace VoxelLandWorldCollision;
	check(IsInGameThread());
	const UWorld* World = GetWorld();
	if (!State || !World || !LocalBounds.IsValid || LocalBounds.Min.X > LocalBounds.Max.X
		|| LocalBounds.Min.Y > LocalBounds.Max.Y || LocalBounds.Min.Z > LocalBounds.Max.Z)
	{
		return false;
	}
	FVector MinLogical;
	FVector MaxLogical;
	if (!VoxelLandWorldCoordinates::ToGlobal(LocalBounds.Min, World->OriginLocation, MinLogical)
		|| !VoxelLandWorldCoordinates::ToGlobal(LocalBounds.Max, World->OriginLocation, MaxLogical))
	{
		return false;
	}

	const TOptional<FVoxelLandDensityBounds>& FiniteBounds = State->Generator->GetDirectDensityBounds();
	const FVoxelLandCollisionField Coordinates(State->Generator->GetProfile().VoxelSizeCm, {}, FiniteBounds);
	FInt64Vector MinCell;
	FInt64Vector MaxCell;
	if (!Coordinates.TryGetCell(MinLogical, MinCell) || !Coordinates.TryGetCell(MaxLogical, MaxCell))
	{
		return false;
	}
	// The sweep can include both cells touching an AABB endpoint after numerical padding.
	MinCell -= FInt64Vector(1);
	MaxCell += FInt64Vector(1);
	if (FiniteBounds)
	{
		constexpr int64 Cells = FVoxelLandChunkData::Cells;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const int64 FirstBoundaryCell = int64(FiniteBounds->MinChunk[Axis]) * Cells - 1;
			const int64 LastBoundaryCell = int64(FiniteBounds->MaxChunkExclusive[Axis]) * Cells;
			MinCell[Axis] = FMath::Max(MinCell[Axis], FirstBoundaryCell);
			MaxCell[Axis] = FMath::Min(MaxCell[Axis], LastBoundaryCell);
			if (MinCell[Axis] > MaxCell[Axis])
			{
				return UpdateCollisionDemand(ConsumerId, {});
			}
		}
	}
	TOptional<FVoxelLandChunkKey> FirstKey;
	TOptional<FVoxelLandChunkKey> LastKey;
	if (!Coordinates.TryGetCellDependency(MinCell, FirstKey) || !Coordinates.TryGetCellDependency(MaxCell, LastKey)
		|| !FirstKey || !LastKey)
	{
		return false;
	}

	int32 Count = 1;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int64 Span = int64(LastKey->Coordinate[Axis]) - FirstKey->Coordinate[Axis] + 1;
		if (Span <= 0 || Span > MaxKeysPerConsumer / Count)
		{
			return false;
		}
		Count *= static_cast<int32>(Span);
	}
	TArray<FVoxelLandChunkKey, TInlineAllocator<MaxKeysPerConsumer>> Keys;
	Keys.Reserve(Count);
	for (int64 Z = FirstKey->Coordinate.Z; Z <= LastKey->Coordinate.Z; ++Z)
	{
		for (int64 Y = FirstKey->Coordinate.Y; Y <= LastKey->Coordinate.Y; ++Y)
		{
			for (int64 X = FirstKey->Coordinate.X; X <= LastKey->Coordinate.X; ++X)
			{
				FVoxelLandChunkKey& Key = Keys.AddDefaulted_GetRef();
				Key.Coordinate = FIntVector(static_cast<int32>(X), static_cast<int32>(Y), static_cast<int32>(Z));
			}
		}
	}
	return UpdateCollisionDemand(ConsumerId, Keys);
}

void AVoxelLandWorld::ReleaseCollisionDemand(const FGuid& ConsumerId)
{
	check(IsInGameThread());
	TSet<FVoxelLandChunkKey> Previous;
	if (!State || !State->CollisionLeases.RemoveAndCopyValue(ConsumerId, Previous))
	{
		return;
	}
	for (const FVoxelLandChunkKey& Key : Previous)
	{
		VoxelLandWorldPrivate::FRecord& Record = State->Records.FindChecked(Key);
		check(Record.CollisionOwners > 0);
		--Record.CollisionOwners;
	}
	State->bDemandComplete = false;
}

bool AVoxelLandWorld::CaptureCollisionView(const FGuid& ConsumerId, FVoxelLandCollisionWorldView& Out) const
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_CaptureCollisionView);
	const UWorld* World = GetWorld();
	const TSet<FVoxelLandChunkKey>* Keys = State ? State->CollisionLeases.Find(ConsumerId) : nullptr;
	if (!World || !Keys)
	{
		return false;
	}
	FVoxelLandCollisionField::FChunkMap Chunks;
	Chunks.Reserve(Keys->Num());
	for (const FVoxelLandChunkKey& Key : *Keys)
	{
		const VoxelLandWorldPrivate::FRecord& Record = State->Records.FindChecked(Key);
		if (Record.Data)
		{
			Chunks.Add(Key, Record.Data);
		}
	}
	FVoxelLandCollisionWorldView View;
	View.Field = MakeShared<FVoxelLandCollisionField, ESPMode::ThreadSafe>(
		State->Generator->GetProfile().VoxelSizeCm, MoveTemp(Chunks), State->Generator->GetDirectDensityBounds());
	View.OriginLocation = World->OriginLocation;
	View.WorldKey = FObjectKey(this);
	View.ConfigurationEpoch = ConfigurationEpoch;
	Out = MoveTemp(View);
	return true;
}

bool AVoxelLandWorld::IsCollisionViewCurrent(const FVoxelLandCollisionWorldView& View) const
{
	check(IsInGameThread());
	const UWorld* World = GetWorld();
	return State && World && View.Field && View.WorldKey == FObjectKey(this)
		&& View.ConfigurationEpoch == ConfigurationEpoch && View.OriginLocation == World->OriginLocation;
}
