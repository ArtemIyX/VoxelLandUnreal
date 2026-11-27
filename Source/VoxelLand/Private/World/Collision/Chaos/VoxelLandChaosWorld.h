#pragma once

#include "Containers/Queue.h"
#include "Data/Collision/Chaos/VoxelLandChaosCollisionOverride.h"
#include "VoxelLandChaosCookCompletion.h"
#include "VoxelLandChaosLease.h"
#include "VoxelLandChaosMeshCompletion.h"
#include "VoxelLandChaosProps.h"
#include "VoxelLandChaosRetirement.h"
#include "World/Collision/Chaos/VoxelLandChaosCookBodySettings.h"
#include "World/Collision/Chaos/VoxelLandChaosCookHookForTests.h"
#include "VoxelLandChaosTile.h"
#include "World/Collision/Chaos/VoxelLandChaosReadiness.h"
#include "World/Collision/Chaos/VoxelLandChaosStats.h"

class AVoxelLandWorld;
class AActor;
class UPrimitiveComponent;
class FVoxelLandChaosViewport;
struct FVoxelLandCollisionWorldView;
struct FVoxelLandChaosTileInfo;

class FVoxelLandChaosWorld final
{
public:
	explicit FVoxelLandChaosWorld(AVoxelLandWorld& InWorld);
	~FVoxelLandChaosWorld();
	bool Enable(const FVoxelLandChaosCollisionSettings& InSettings, FString& OutError);
	void Disable(bool bDestroyBodies, bool bWorldEnds);
	void Tick();
	bool IsEnabled() const { return bEnabled; }
	bool HasOutstanding() const;
	EVoxelLandChaosReadiness UpdateLease(const FGuid& Owner, TConstArrayView<FVoxelLandChunkKey> Keys);
	void ReleaseLease(const FGuid& Owner);
	EVoxelLandChaosReadiness GetReadiness(const FGuid& Owner) const;
	bool CaptureView(const FGuid& Owner, FVoxelLandCollisionWorldView& Out) const;
	bool SetOverride(TConstArrayView<FVoxelLandChunkKey> Keys, const FVoxelLandChaosCollisionOverride& Override,
		uint64 PolicyRevision = 0);
	bool GetTileInfo(const FVoxelLandChunkKey& Key, FVoxelLandChaosTileInfo& Out) const;
	bool Rebuild(TConstArrayView<FVoxelLandChunkKey> Keys, uint64 SourceRevision);
	bool RegisterProp(UPrimitiveComponent& Component, FGuid& OutOwner, bool bSimulateWhenReady);
	void UnregisterProp(const FGuid& Owner);
	FVoxelLandChaosStats GetStats() const;
#if WITH_DEV_AUTOMATION_TESTS
	void SetCookHookForTests(FVoxelLandChaosCookHookForTests Hook) { CookHookForTests = MoveTemp(Hook); }
#endif

private:
	using FMeshQueue = TQueue<FVoxelLandChaosMeshCompletion, EQueueMode::Mpsc>;
	using FCookQueue = TQueue<FVoxelLandChaosCookCompletion>;

	static constexpr int32 MaxLeaseKeys = 64;
	static constexpr int32 MaxRuntimeOverrides = 2048;
	static constexpr int32 MaxRebuildKeys = 64;
	static constexpr int32 MaxAttempts = 3;

	bool SyncPins(FVoxelLandChaosLease& Lease);
	void ReleaseTiles(const TSet<FVoxelLandChunkKey>& Old, const TSet<FVoxelLandChunkKey>& Keep);
	EVoxelLandChaosCollisionDetail ResolveDetail(const FVoxelLandChunkKey& Key) const;
	void Invalidate(FVoxelLandChaosTile& Tile, bool bSourceChanged);
	void RecordFailure(FVoxelLandChaosTile& Tile);
	void DrainCompletions();
	void RefreshDemand();
	void SubmitMesh(const FVoxelLandChunkKey& Key, FVoxelLandChaosTile& Tile,
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Field);
	void SubmitCook(const FVoxelLandChunkKey& Key, FVoxelLandChaosTile& Tile);
	bool Install(const FVoxelLandChunkKey& Key, FVoxelLandChaosTile& Tile);
	bool QueueRetirement(FVoxelLandChaosTile& Tile);
	void AdvanceRetirements(const double Deadline, int32& PhysicsUpdates);
	void AdvanceTile(
		const FVoxelLandChunkKey& Key, FVoxelLandChaosTile& Tile, const double Deadline, int32& PhysicsUpdates);
	AActor* GetBodyOwner();
	int64 GetAwaitingBytes() const;
	int64 GetCookedAwaitingInstallBytes() const;

	AVoxelLandWorld& World;
	FVoxelLandChaosCollisionSettings Settings;
	FVoxelLandChaosCookBodySettings BodySettings;
	FVoxelLandChaosProps Props;
#if !UE_SERVER && !UE_BUILD_SHIPPING
	TUniquePtr<FVoxelLandChaosViewport> Viewport;
#endif
	TWeakObjectPtr<AActor> BodyOwner;
	TMap<FVoxelLandChunkKey, FVoxelLandChaosTile> Tiles;
	TMap<FGuid, FVoxelLandChaosLease> Leases;
	TMap<FVoxelLandChunkKey, FVoxelLandChaosCollisionOverride> Overrides;
	TArray<FVoxelLandChaosRetirement> RetiredBodies;
	TSharedRef<FMeshQueue, ESPMode::ThreadSafe> MeshCompletions = MakeShared<FMeshQueue, ESPMode::ThreadSafe>();
	TSharedRef<FCookQueue, ESPMode::ThreadSafe> CookCompletions = MakeShared<FCookQueue, ESPMode::ThreadSafe>();
	uint64 Ticket = 0;
	uint64 LatestSourceRevision = 0;
	uint64 LatestPolicyRevision = 0;
	TMap<uint64, int64> PendingCookTickets;
#if WITH_DEV_AUTOMATION_TESTS
	FVoxelLandChaosCookHookForTests CookHookForTests;
#endif
	int32 MeshActive = 0;
	int32 CookActive = 0;
	int64 CookBytes = 0;
	int64 ResidentBytes = 0;
	int64 StaleResults = 0;
	int64 Failures = 0;
	double NextPinsRetry = 0.0;
	bool bPinsNeedRefresh = false;
	bool bEnabled = false;
};
