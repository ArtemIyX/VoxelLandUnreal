#include "World/VoxelLandWorld.h"

#include "VoxelLandChaosWorld.h"
#include "../../Streaming/Clipmap/VoxelLandClipmapRenderWork.h"
#include "Data/World/Streaming/VoxelLandWorldState.h"
#include "Engine/World.h"

bool AVoxelLandWorld::SetChaosTerrainCollisionEnabled(const bool bEnabled)
{
	check(IsInGameThread());
	if (bEnabled == (ChaosCollision && ChaosCollision->IsEnabled()))
	{
		return true;
	}
	FVoxelLandChaosCollisionSettings Settings = ChaosCollisionSettings;
	Settings.bEnabled = bEnabled;
	FString Error;
	if (!ConfigureChaosTerrainCollision(Settings, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelLand Chaos collision: %s"), *Error);
		return false;
	}
	return true;
}

bool AVoxelLandWorld::ConfigureChaosTerrainCollision(
	const FVoxelLandChaosCollisionSettings& Settings, FString& OutError)
{
	check(IsInGameThread());
	OutError.Reset();
	if (!Settings.bEnabled)
	{
		ChaosCollisionSettings = Settings;
		if (ChaosCollision)
		{
			ChaosCollision->Disable(false, false);
		}
		return true;
	}
	if (!ChaosCollision)
	{
		TUniquePtr<FVoxelLandChaosWorld> Candidate = MakeUnique<FVoxelLandChaosWorld>(*this);
		if (!Candidate->Enable(Settings, OutError))
		{
			return false;
		}
		ChaosCollision = MoveTemp(Candidate);
	}
	else if (!ChaosCollision->Enable(Settings, OutError))
	{
		return false;
	}
	ChaosCollisionSettings = Settings;
	return true;
}

void AVoxelLandWorld::StopChaosCollision(const bool bDestroyBodies)
{
	if (ChaosCollision)
	{
		const bool bWorldEnds = bLifecycleEnded || !GetWorld() || GetWorld()->bIsTearingDown;
		ChaosCollision->Disable(bDestroyBodies, bWorldEnds);
		if (bWorldEnds || (!ChaosCollision->HasOutstanding() && !ChaosCollision->HasRetainedState()))
		{
			ChaosCollision.Reset();
		}
		else
		{
			ChaosCollision->DrainAfterActorShutdown();
		}
	}
}

void AVoxelLandWorld::AdvanceChaosCollision()
{
	if (ChaosCollision->IsEnabled())
	{
		ChaosCollision->Tick();
		return;
	}
	if (ChaosCollision->HasOutstanding())
	{
		ChaosCollision->Tick();
	}
	// Runtime deactivation retains accepted policy, while shutdown clears it through Disable.
	if (!ChaosCollision->HasOutstanding() && !ChaosCollision->HasRetainedState())
	{
		ChaosCollision.Reset();
	}
}

EVoxelLandChaosReadiness AVoxelLandWorld::UpdateChaosCollisionLease(const FGuid& ConsumerId, const FBox& LocalBounds)
{
	check(IsInGameThread());
	if (!ChaosCollision || !ChaosCollision->IsEnabled())
	{
		return EVoxelLandChaosReadiness::Disabled;
	}
	TArray<FVoxelLandChunkKey> Keys;
	if (!CollectCollisionDemandKeys(LocalBounds, Keys))
	{
		return EVoxelLandChaosReadiness::BudgetExceeded;
	}
	return ChaosCollision->UpdateLease(ConsumerId, Keys);
}

EVoxelLandChaosReadiness AVoxelLandWorld::UpdateChaosCollisionTiles(
	const FGuid& ConsumerId, TConstArrayView<FVoxelLandChunkKey> Tiles)
{
	check(IsInGameThread());
	return ChaosCollision ? ChaosCollision->UpdateLease(ConsumerId, Tiles) : EVoxelLandChaosReadiness::Disabled;
}

void AVoxelLandWorld::ReleaseChaosCollisionLease(const FGuid& ConsumerId)
{
	check(IsInGameThread());
	if (ChaosCollision)
	{
		ChaosCollision->ReleaseLease(ConsumerId);
	}
}

EVoxelLandChaosReadiness AVoxelLandWorld::GetChaosCollisionReadiness(const FGuid& ConsumerId) const
{
	check(IsInGameThread());
	return ChaosCollision ? ChaosCollision->GetReadiness(ConsumerId) : EVoxelLandChaosReadiness::Disabled;
}

bool AVoxelLandWorld::CaptureChaosCollisionView(const FGuid& ConsumerId, FVoxelLandCollisionWorldView& Out) const
{
	check(IsInGameThread());
	return ChaosCollision && ChaosCollision->CaptureView(ConsumerId, Out);
}

bool AVoxelLandWorld::SetChaosCollisionOverride(
	TConstArrayView<FVoxelLandChunkKey> Tiles, const FVoxelLandChaosCollisionOverride& Override)
{
	check(IsInGameThread());
	return ChaosCollision && ChaosCollision->IsEnabled() && ChaosCollision->SetOverride(Tiles, Override);
}

bool AVoxelLandWorld::SetChaosCollisionTileOverride(
	const FIntVector Coordinate, const FVoxelLandChaosCollisionOverride& Override)
{
	const FVoxelLandChunkKey Key{Coordinate, 0};
	return SetChaosCollisionOverride(MakeArrayView(&Key, 1), Override);
}

bool AVoxelLandWorld::ApplyChaosCollisionOverrideRevision(TConstArrayView<FVoxelLandChunkKey> Tiles,
	const FVoxelLandChaosCollisionOverride& Override, const uint64 PolicyRevision)
{
	check(IsInGameThread());
	return PolicyRevision != 0 && ChaosCollision && ChaosCollision->IsEnabled()
		&& ChaosCollision->SetOverride(Tiles, Override, PolicyRevision);
}

bool AVoxelLandWorld::GetChaosCollisionTileInfo(const FVoxelLandChunkKey& Key, FVoxelLandChaosTileInfo& Out) const
{
	check(IsInGameThread());
	return ChaosCollision && ChaosCollision->GetTileInfo(Key, Out);
}

bool AVoxelLandWorld::SetChaosCollisionBoundsOverride(
	const FBox& LocalBounds, const FVoxelLandChaosCollisionOverride& Override)
{
	TArray<FVoxelLandChunkKey> Keys;
	return CollectCollisionDemandKeys(LocalBounds, Keys) && SetChaosCollisionOverride(Keys, Override);
}

bool AVoxelLandWorld::RequestChaosCollisionRebuildForBounds(const FBox& LocalBounds)
{
	TArray<FVoxelLandChunkKey> Keys;
	return CollectCollisionDemandKeys(LocalBounds, Keys) && RequestChaosCollisionRebuild(Keys);
}

bool AVoxelLandWorld::RequestChaosCollisionRebuild(TConstArrayView<FVoxelLandChunkKey> Tiles)
{
	check(IsInGameThread());
	return ChaosCollision && ChaosCollision->Rebuild(Tiles, 0);
}

bool AVoxelLandWorld::NotifyChaosCollisionSourceChanged(
	TConstArrayView<FVoxelLandChunkKey> Tiles, const uint64 SourceRevision)
{
	check(IsInGameThread());
	return SourceRevision != 0 && ChaosCollision && ChaosCollision->Rebuild(Tiles, SourceRevision);
}

bool AVoxelLandWorld::RegisterChaosProp(
	UPrimitiveComponent& Component, FGuid& OutConsumerId, const bool bSimulateWhenReady)
{
	check(IsInGameThread());
	return ChaosCollision && ChaosCollision->RegisterProp(Component, OutConsumerId, bSimulateWhenReady);
}

void AVoxelLandWorld::UnregisterChaosProp(const FGuid& ConsumerId)
{
	check(IsInGameThread());
	if (ChaosCollision)
	{
		ChaosCollision->UnregisterProp(ConsumerId);
	}
}

FVoxelLandChaosStats AVoxelLandWorld::GetChaosCollisionStats() const
{
	check(IsInGameThread());
	return ChaosCollision ? ChaosCollision->GetStats() : FVoxelLandChaosStats{};
}

#if WITH_DEV_AUTOMATION_TESTS
void AVoxelLandWorld::SetChaosCookHookForTests(FVoxelLandChaosCookHookForTests Hook)
{
	check(IsInGameThread());
	if (ChaosCollision)
	{
		ChaosCollision->SetCookHookForTests(MoveTemp(Hook));
	}
	else
	{
		check(!Hook);
	}
}
bool AVoxelLandWorld::FailChaosFieldForTests(const FVoxelLandChunkKey& Key)
{
	check(IsInGameThread());
	VoxelLandWorldPrivate::FRecord* Record = State ? State->Records.Find(Key) : nullptr;
	if (!Record || Record->Data || Record->bRunning || Record->CollisionOwners == 0)
	{
		return false;
	}
	Record->bQueued = false;
	Record->bFailed = true;
	Record->CollisionRetries = VoxelLandWorldPrivate::FRecord::MaxCollisionRetries;
	Record->Error = TEXT("Injected terminal terrain field failure.");
	return true;
}

bool AVoxelLandWorld::ReplaceChaosFieldForTests(TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Field)
{
	check(IsInGameThread());
	if (!State || !Field || Field->Key.LOD != 0 || Field->Density.Num() != FVoxelLandChunkData::SampleCount)
	{
		return false;
	}
	VoxelLandWorldPrivate::FRecord* Record = State->Records.Find(Field->Key);
	if (!Record || !Record->Data || Record->bRunning)
	{
		return false;
	}
	Record->Data = MoveTemp(Field);
	return true;
}
#endif

int32 AVoxelLandWorld::ResolveChaosRenderLod(const FVoxelLandChunkKey& Key) const
{
	if (!State || State->bServer)
	{
		return INDEX_NONE;
	}
	const VoxelLandWorldPrivate::FM7VisualState* Visual = State->M7Visual.Get();
	if (!Visual)
	{
		const VoxelLandWorldPrivate::FRecord* Record = State->Records.Find(Key);
		return Record && Record->MeshBytes > 0 ? 0 : INDEX_NONE;
	}
	if (!Visual->Published)
	{
		return INDEX_NONE;
	}
	const VoxelLandWorldPrivate::FM7RenderRevision& Revision = *Visual->Published;
	for (uint8 Lod = 0; Lod <= Revision.Layout.Settings.MaxLod; ++Lod)
	{
		const int32 Scale = 1 << Lod;
		const FVoxelLandChunkKey VisualKey{
			FIntVector(VoxelLandCoordinates::FloorDivide(Key.Coordinate.X, Scale),
				VoxelLandCoordinates::FloorDivide(Key.Coordinate.Y, Scale),
				Revision.bHeightSurface ? 0 : VoxelLandCoordinates::FloorDivide(Key.Coordinate.Z, Scale)),
			Lod};
		if (Revision.bHeightSurface ? Revision.HeightDrawIndices.Contains(VisualKey)
									: Revision.Sources.Contains(VisualKey))
		{
			return Lod;
		}
	}
	return INDEX_NONE;
}
