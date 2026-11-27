#include "VoxelLandChaosWorld.h"

#include "Data/Collision/Chaos/VoxelLandChaosCollisionOverride.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "World/Collision/Chaos/VoxelLandChaosTerrainComponent.h"
#include "World/Collision/Chaos/VoxelLandChaosTileInfo.h"
#include "World/VoxelLandWorld.h"
#if !UE_SERVER && !UE_BUILD_SHIPPING
#include "../../Demo/VoxelLandChaosViewport.h"
#endif

namespace VoxelLandChaosPolicy
{
ECollisionChannel FindChannel(const UCollisionProfile& Profiles, const FName Name)
{
	for (int32 Index = 0; Index < ECC_MAX; ++Index)
	{
		if (Profiles.ReturnChannelNameFromContainerIndex(Index) == Name)
		{
			return static_cast<ECollisionChannel>(Index);
		}
	}
	return ECC_MAX;
}

bool ResolveBodySettings(
	const FVoxelLandChaosCollisionSettings& Settings, FVoxelLandChaosCookBodySettings& Out, FString& OutError)
{
	const UCollisionProfile* Profiles = UCollisionProfile::Get();
	if (!Profiles)
	{
		OutError = TEXT("Unreal collision profiles are unavailable.");
		return false;
	}

	Out.CollisionEnabled = Settings.CollisionMode.GetValue();
	Out.ObjectType = ECC_WorldStatic;
	Out.Responses.SetAllChannels(ECR_Block);
	if (!Settings.CollisionProfile.IsNone())
	{
		FCollisionResponseTemplate Profile;
		if (!Profiles->GetProfileTemplate(Settings.CollisionProfile, Profile))
		{
			OutError = FString::Printf(TEXT("Unknown collision profile '%s'."), *Settings.CollisionProfile.ToString());
			return false;
		}
		Out.ObjectType = Profile.ObjectType.GetValue();
		Out.Responses = Profile.ResponseToChannels;
	}
	if (!Settings.ObjectType.IsNone())
	{
		Out.ObjectType = FindChannel(*Profiles, Settings.ObjectType);
	}
	if (Out.ObjectType == ECC_MAX || Profiles->ConvertToObjectType(Out.ObjectType) == ObjectTypeQuery_MAX)
	{
		OutError = FString::Printf(TEXT("Invalid collision object type '%s'."), *Settings.ObjectType.ToString());
		return false;
	}
	for (int32 Index = 0; Index < ECC_MAX; ++Index)
	{
		const ECollisionChannel Channel = static_cast<ECollisionChannel>(Index);
		if (Out.Responses.GetResponse(Channel) == ECR_Overlap)
		{
			Out.Responses.SetResponse(Channel, ECR_Ignore);
		}
	}
	for (const FVoxelLandChaosCollisionResponse& Entry : Settings.Responses)
	{
		const ECollisionChannel Channel = FindChannel(*Profiles, Entry.Channel);
		if (Channel == ECC_MAX)
		{
			OutError = FString::Printf(TEXT("Unknown collision response channel '%s'."), *Entry.Channel.ToString());
			return false;
		}
		Out.Responses.SetResponse(Channel, Entry.Response.GetValue());
	}
	return true;
}
}

FVoxelLandChaosWorld::FVoxelLandChaosWorld(AVoxelLandWorld& InWorld)
	: World(InWorld)
{
}

FVoxelLandChaosWorld::~FVoxelLandChaosWorld() = default;

bool FVoxelLandChaosWorld::Enable(const FVoxelLandChaosCollisionSettings& InSettings, FString& OutError)
{
	check(IsInGameThread());
	UWorld* SceneWorld = World.GetWorld();
	if (!InSettings.Validate(OutError))
	{
		return false;
	}
	if (!World.State || !SceneWorld || SceneWorld->bIsTearingDown || !SceneWorld->GetPhysicsScene())
	{
		OutError = TEXT("An initialized voxel world with an active physics scene is required.");
		return false;
	}
	FVoxelLandChaosCookBodySettings Resolved;
	if (!VoxelLandChaosPolicy::ResolveBodySettings(InSettings, Resolved, OutError))
	{
		return false;
	}
#if !UE_SERVER && !UE_BUILD_SHIPPING
	Resolved.bDrawCollision = !World.bDataOnly && World.GetNetMode() != NM_DedicatedServer && FApp::CanEverRender();
#endif
	if (bEnabled)
	{
		Disable(false, false);
	}
	Settings = InSettings;
	BodySettings = Resolved;
	bEnabled = true;
#if !UE_SERVER && !UE_BUILD_SHIPPING
	if (Resolved.bDrawCollision)
	{
		Viewport = MakeUnique<FVoxelLandChaosViewport>(World);
	}
#endif
	return true;
}

void FVoxelLandChaosWorld::Disable(const bool bDestroyBodies, const bool bWorldEnds)
{
	check(IsInGameThread());
	bEnabled = false;
#if !UE_SERVER && !UE_BUILD_SHIPPING
	Viewport.Reset();
#endif
	Props.SuspendAll();
	for (const TPair<FGuid, FVoxelLandChaosLease>& Entry : Leases)
	{
		World.ReleaseCollisionDemand(Entry.Value.FieldOwner);
	}
	Leases.Reset();
	for (TPair<FVoxelLandChunkKey, FVoxelLandChaosTile>& Entry : Tiles)
	{
		Invalidate(Entry.Value, true);
		QueueRetirement(Entry.Value);
	}
	Tiles.Reset();
	if (bDestroyBodies || bWorldEnds)
	{
		if (bDestroyBodies && BodyOwner.IsValid())
		{
			BodyOwner->Destroy();
		}
		BodyOwner.Reset();
		RetiredBodies.Reset();
		ResidentBytes = 0;
		LatestSourceRevision = 0;
		LatestPolicyRevision = 0;
		Overrides.Reset();
	}
	if (bWorldEnds)
	{
		Props.Reset();
	}
}

bool FVoxelLandChaosWorld::HasOutstanding() const
{
	return MeshActive > 0 || CookActive > 0 || !RetiredBodies.IsEmpty() || Props.HasManagedProps();
}

EVoxelLandChaosCollisionDetail FVoxelLandChaosWorld::ResolveDetail(const FVoxelLandChunkKey& Key) const
{
	const FVoxelLandChaosTile* Tile = Tiles.Find(Key);
	int32 RenderLod = INDEX_NONE;
	if (Settings.bMapByRenderLod)
	{
		RenderLod = Tile && Tile->bHasVisualLod ? Tile->StableVisualLod : World.ResolveChaosRenderLod(Key);
	}
	return Settings.ResolveDetail(RenderLod, Overrides.Find(Key));
}

bool FVoxelLandChaosWorld::SyncPins(FVoxelLandChaosLease& Lease)
{
	TSet<FVoxelLandChunkKey> Required;
	for (const FVoxelLandChunkKey& Key : Lease.Tiles)
	{
		if (ResolveDetail(Key) != EVoxelLandChaosCollisionDetail::Disabled)
		{
			Required.Add(Key);
		}
	}
	if (Required.Num() == Lease.Pinned.Num() && Required.Includes(Lease.Pinned))
	{
		return true;
	}
	if (Required.IsEmpty())
	{
		World.ReleaseCollisionDemand(Lease.FieldOwner);
	}
	else if (!World.UpdateCollisionDemand(Lease.FieldOwner, Required.Array()))
	{
		return false;
	}
	Lease.Pinned = MoveTemp(Required);
	return true;
}

EVoxelLandChaosReadiness FVoxelLandChaosWorld::UpdateLease(const FGuid& Owner, TConstArrayView<FVoxelLandChunkKey> Keys)
{
	if (!bEnabled)
	{
		return EVoxelLandChaosReadiness::Disabled;
	}
	if (!Owner.IsValid() || Keys.Num() > MaxLeaseKeys)
	{
		return EVoxelLandChaosReadiness::BudgetExceeded;
	}
	const FVoxelLandChaosLease* Previous = Leases.Find(Owner);
	static constexpr int32 MaxOwners = 16;
	if (!Previous && Leases.Num() >= MaxOwners)
	{
		return EVoxelLandChaosReadiness::BudgetExceeded;
	}

	FVoxelLandChaosLease Candidate;
	Candidate.FieldOwner = Previous ? Previous->FieldOwner : FGuid::NewGuid();
	Candidate.Pinned = Previous ? Previous->Pinned : TSet<FVoxelLandChunkKey>{};
	Candidate.Tiles.Reserve(Keys.Num());
	for (const FVoxelLandChunkKey& Key : Keys)
	{
		Candidate.Tiles.Add(Key);
	}
	if (Previous && Candidate.Tiles.Num() == Previous->Tiles.Num() && Candidate.Tiles.Includes(Previous->Tiles))
	{
		return GetReadiness(Owner);
	}
	int32 NewTiles = 0;
	for (const FVoxelLandChunkKey& Key : Candidate.Tiles)
	{
		if ((!Previous || !Previous->Tiles.Contains(Key)) && (Key.LOD != 0 || !World.CanRequestChunk(Key)))
		{
			return EVoxelLandChaosReadiness::Unsupported;
		}
		NewTiles += Tiles.Contains(Key) ? 0 : 1;
	}
	if (Tiles.Num() + NewTiles > Settings.MaxLeasedTiles || !SyncPins(Candidate))
	{
		return EVoxelLandChaosReadiness::BudgetExceeded;
	}
	for (const FVoxelLandChunkKey& Key : Candidate.Tiles)
	{
		if (!Previous || !Previous->Tiles.Contains(Key))
		{
			FVoxelLandChaosTile& Tile = Tiles.FindOrAdd(Key);
			++Tile.Owners;
			Tile.RetirementTime = 0.0;
		}
	}
	if (Previous)
	{
		ReleaseTiles(Previous->Tiles, Candidate.Tiles);
	}
	Leases.Add(Owner, MoveTemp(Candidate));
	return GetReadiness(Owner);
}

void FVoxelLandChaosWorld::ReleaseTiles(const TSet<FVoxelLandChunkKey>& Old, const TSet<FVoxelLandChunkKey>& Keep)
{
	const double Retirement = FPlatformTime::Seconds() + Settings.RetirementGraceSeconds;
	for (const FVoxelLandChunkKey& Key : Old)
	{
		if (!Keep.Contains(Key))
		{
			FVoxelLandChaosTile& Tile = Tiles.FindChecked(Key);
			if (--Tile.Owners == 0)
			{
				Tile.RetirementTime = Retirement;
				// Keep installed coverage for a returning consumer, but cancel pending replacements.
				Invalidate(Tile, false);
				if (Tile.bHasCoverage)
				{
					Tile.Phase = EVoxelLandChaosPhase::Ready;
				}
			}
		}
	}
}

void FVoxelLandChaosWorld::ReleaseLease(const FGuid& Owner)
{
	FVoxelLandChaosLease Lease;
	if (Leases.RemoveAndCopyValue(Owner, Lease))
	{
		World.ReleaseCollisionDemand(Lease.FieldOwner);
		ReleaseTiles(Lease.Tiles, {});
	}
}

EVoxelLandChaosReadiness FVoxelLandChaosWorld::GetReadiness(const FGuid& Owner) const
{
	if (!bEnabled)
	{
		return EVoxelLandChaosReadiness::Disabled;
	}
	const FVoxelLandChaosLease* Lease = Leases.Find(Owner);
	if (!Lease)
	{
		return EVoxelLandChaosReadiness::Pending;
	}
	bool bFailed = false;
	bool bPending = false;
	bool bOutOfBudget = false;
	for (const FVoxelLandChunkKey& Key : Lease->Tiles)
	{
		if (ResolveDetail(Key) == EVoxelLandChaosCollisionDetail::Disabled)
		{
			return EVoxelLandChaosReadiness::Disabled;
		}
		const FVoxelLandChaosTile* Tile = Tiles.Find(Key);
		if (!Tile || !Tile->bHasCoverage || (Tile->ResidentBytes > 0 && !Tile->Body.IsValid()))
		{
			bFailed |= Tile && Tile->Phase == EVoxelLandChaosPhase::Failed && Tile->Attempts >= MaxAttempts;
			bOutOfBudget |= Tile && Tile->bInstallOverBudget;
			bPending = true;
		}
	}
	if (bFailed)
	{
		return EVoxelLandChaosReadiness::Failed;
	}
	if (bOutOfBudget)
	{
		return EVoxelLandChaosReadiness::BudgetExceeded;
	}
	return bPending ? EVoxelLandChaosReadiness::Pending : EVoxelLandChaosReadiness::Ready;
}

bool FVoxelLandChaosWorld::CaptureView(const FGuid& Owner, FVoxelLandCollisionWorldView& Out) const
{
	const FVoxelLandChaosLease* Lease = Leases.Find(Owner);
	return Lease && World.CaptureCollisionView(Lease->FieldOwner, Out);
}

bool FVoxelLandChaosWorld::SetOverride(
	TConstArrayView<FVoxelLandChunkKey> Keys, const FVoxelLandChaosCollisionOverride& Override,
	const uint64 PolicyRevision)
{
	if (PolicyRevision != 0 && PolicyRevision <= LatestPolicyRevision)
	{
		return PolicyRevision == LatestPolicyRevision;
	}
	if (!Override.IsValid() || Keys.Num() > MaxLeaseKeys)
	{
		return false;
	}
	TSet<FVoxelLandChunkKey> Unique;
	int32 NewOverrides = 0;
	for (const FVoxelLandChunkKey& Key : Keys)
	{
		if (Key.LOD != 0)
		{
			return false;
		}
		if (!Unique.Contains(Key))
		{
			Unique.Add(Key);
			NewOverrides += Overrides.Contains(Key) ? 0 : 1;
		}
	}
	if (Override.Mode != EVoxelLandChaosCollisionOverrideMode::Inherit
		&& Overrides.Num() + NewOverrides > MaxRuntimeOverrides)
	{
		return false;
	}
	for (const FVoxelLandChunkKey& Key : Unique)
	{
		if (Override.Mode == EVoxelLandChaosCollisionOverrideMode::Inherit)
		{
			Overrides.Remove(Key);
		}
		else
		{
			Overrides.Add(Key, Override);
		}
	}
	LatestPolicyRevision = FMath::Max(LatestPolicyRevision, PolicyRevision);
	bPinsNeedRefresh = true;
	NextPinsRetry = 0.0;
	return true;
}

bool FVoxelLandChaosWorld::GetTileInfo(const FVoxelLandChunkKey& Key, FVoxelLandChaosTileInfo& Out) const
{
	const FVoxelLandChaosTile* Tile = Tiles.Find(Key);
	if (!bEnabled || !Tile)
	{
		return false;
	}
	Out.RequestedLod = FVoxelLandChaosCollisionSettings::GetLod(ResolveDetail(Key));
	Out.InstalledLod = Tile->InstalledLod;
	Out.SourceRevision = Tile->SourceRevision;
	Out.bHasShape = Tile->Body.IsValid();
	Out.bCoverageReady = Tile->bHasCoverage && Out.RequestedLod != INDEX_NONE
		&& (Tile->ResidentBytes == 0 || Out.bHasShape);
	Out.bInstallOverBudget = Tile->bInstallOverBudget;
	return true;
}

bool FVoxelLandChaosWorld::Rebuild(TConstArrayView<FVoxelLandChunkKey> Keys, const uint64 SourceRevision)
{
	if (!bEnabled || Keys.IsEmpty() || Keys.Num() > MaxRebuildKeys
		|| (SourceRevision != 0 && SourceRevision < LatestSourceRevision))
	{
		return false;
	}
	TSet<FVoxelLandChunkKey> Affected;
	for (const FVoxelLandChunkKey& Key : Keys)
	{
		if (Key.LOD != 0)
		{
			return false;
		}
		for (int32 Z = -1; Z <= 1; ++Z)
		{
			for (int32 Y = -1; Y <= 1; ++Y)
			{
				for (int32 X = -1; X <= 1; ++X)
				{
					const FInt64Vector Neighbor = FInt64Vector(Key.Coordinate) + FInt64Vector(X, Y, Z);
					if (Neighbor.GetMin() >= MIN_int32 && Neighbor.GetMax() <= MAX_int32)
					{
						const FVoxelLandChunkKey NeighborKey{FIntVector(Neighbor), 0};
						if (Tiles.Contains(NeighborKey))
						{
							Affected.Add(NeighborKey);
						}
					}
				}
			}
		}
	}
	LatestSourceRevision = FMath::Max(LatestSourceRevision, SourceRevision);
	for (const FVoxelLandChunkKey& Key : Affected)
	{
		FVoxelLandChaosTile& Tile = Tiles.FindChecked(Key);
		if (SourceRevision == 0 || SourceRevision > Tile.SourceRevision)
		{
			Tile.SourceRevision = FMath::Max(SourceRevision, Tile.SourceRevision);
			Invalidate(Tile, SourceRevision != 0);
		}
	}
	return true;
}

bool FVoxelLandChaosWorld::RegisterProp(UPrimitiveComponent& Component, FGuid& OutOwner, const bool bSimulateWhenReady)
{
	return bEnabled && BodySettings.CollisionEnabled == ECollisionEnabled::QueryAndPhysics
		&& Props.Register(World, Component, OutOwner, bSimulateWhenReady);
}

void FVoxelLandChaosWorld::UnregisterProp(const FGuid& Owner)
{
	Props.Unregister(World, Owner);
}
