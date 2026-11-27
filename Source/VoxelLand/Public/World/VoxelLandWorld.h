#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Async/Future.h"
#include "Data/Field/Chunks/VoxelLandChunkData.h"
#include "Data/Collision/Chaos/VoxelLandChaosCollisionSettings.h"
#include "Data/Collision/Chaos/VoxelLandChaosCollisionOverride.h"
#include "World/Collision/Chaos/VoxelLandChaosReadiness.h"
#include "World/Collision/Chaos/VoxelLandChaosCookHookForTests.h"
#include "VoxelLandWorld.generated.h"

class FVoxelLandGenerator;
class FVoxelLandChaosWorld;
struct FVoxelLandChaosStats;
struct FVoxelLandChaosTileInfo;
class UVoxelLandWorldConfig;
class UVoxelLandWorldSource;
class UVoxelLandWorldConfig;
class UPrimitiveComponent;
class FVoxelLandInitializationJob;
class FVoxelLandDemoJob;
class UMaterialInterface;
#if WITH_EDITOR
class FPackageReloadedEvent;
enum class EPackageReloadPhase : uint8;
#endif
struct FVoxelLandM4SavedWorld;
struct FVoxelLandWorldState;
struct FVoxelLandCollisionWorldView;
struct FVoxelLandTestBarrier;
struct FVoxelLandReloadTestBarrier;
namespace VoxelLandWorldPrivate
{
struct FCompletion;
}

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVoxelLandInitializationFinished, bool, bSuccess);

UCLASS(NotBlueprintable, Transient)
class VOXELLAND_API AVoxelLandRegion : public AActor
{
	GENERATED_BODY()
public:
	AVoxelLandRegion();
	bool Publish(const FIntVector& Key, const FVector& Origin,
		TSharedPtr<const struct FVoxelLandMeshData, ESPMode::ThreadSafe> Mesh, UMaterialInterface* Material,
		bool bCurtain = false);
	void Remove(const FIntVector& Key);
	void SetTerrainMaterial(UMaterialInterface* Material);
	bool IsEmpty() const { return Components.IsEmpty() && CurtainComponents.IsEmpty(); }
	bool IsChunkEffectivelyVisible(const FIntVector& Key) const;
	void BeginHeightClipmapStaging();
	TSharedPtr<TAtomic<bool>, ESPMode::ThreadSafe> SetHeightClipmapVisible(bool bVisible);

private:
	TSharedPtr<TAtomic<bool>, ESPMode::ThreadSafe> HeightClipmapVisibility;
	bool bHeightClipmapVisible = true;
	UPROPERTY()
	TObjectPtr<USceneComponent> Root;
	UPROPERTY()
	TMap<FIntVector, TObjectPtr<UPrimitiveComponent>> Components;
	UPROPERTY()
	TMap<FIntVector, TObjectPtr<UPrimitiveComponent>> CurtainComponents;
};

UCLASS(BlueprintType)
class VOXELLAND_API AVoxelLandWorld : public AActor
{
	GENERATED_BODY()
public:
	AVoxelLandWorld();
	AVoxelLandWorld(FVTableHelper& Helper);
	virtual ~AVoxelLandWorld() override;

	virtual void Tick(float DeltaSeconds) override;
#if WITH_EDITOR
	virtual bool ShouldTickIfViewportsOnly() const override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
#if !UE_BUILD_SHIPPING && !UE_SERVER
	bool DescribeClipmapFlightCoverage(
		const FVector& LogicalCamera, TConstArrayView<FVector2D> FixedProbes, uint64& LastTicket, FString& Out) const;
	uint64 GetSettledClipmapFlightTicket(const FVector& LogicalView) const;
#endif

public:
	virtual void Destroyed() override;
	virtual void BeginDestroy() override;
	// Generator snapshots must have been created on a worker thread. Replacing one invalidates prior jobs.
	// A null generator leaves the current world and its work intact.
	void Initialize(TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator);
	// Copies settings on the game thread and initializes asynchronously. No work starts in BeginPlay.
	UFUNCTION(BlueprintCallable, Category = "VoxelLand|Initialization")
	bool InitializeFromConfig(UVoxelLandWorldConfig* Config, FIntVector StartupChunk);
#if WITH_EDITOR
	[[nodiscard]] bool InitializeFiniteEditorPreview(UVoxelLandWorldConfig* Config);
	[[nodiscard]] bool RequestEditorPreviewBounds(const FBox& Bounds, FBox& OutClippedBounds, FString& OutError);
	// Returns true on success or terminal failure. OutError distinguishes the two.
	[[nodiscard]] bool PollEditorTerrainPreview(FString& OutError) const;
	void SetEditorTerrainPreviewVisible(bool bVisible);
#endif
	// Initializes cave-disabled finite definitions asynchronously. The initial
	// chunk is selected near the control-grid center and its demand bubble must fit.
	// Enabled caves and other definitions remain rejected pending preparation.
#if WITH_DEV_AUTOMATION_TESTS
	bool InitializeFiniteSourceForTests(UVoxelLandWorldSource* Definition);
#endif
	// Game-thread copy of the published authored M6 identity. False means no active overlay.
	UFUNCTION(BlueprintCallable, Category = "VoxelLand|Initialization")
	bool GetM6WorldIdentity(TArray<uint8>& OutIdentity) const;
	// The caller supplies an already-deserialized, trusted saved record. Material
	// presentation remains under caller control. Validation runs asynchronously.
	bool InitializeM4FromSavedWorld(const FVoxelLandM4SavedWorld& Saved, FIntVector StartupChunk);
	UFUNCTION(BlueprintCallable, Category = "VoxelLand|Initialization")
	void CancelInitialization();
	UPROPERTY(BlueprintAssignable, Category = "VoxelLand|Initialization")
	FVoxelLandInitializationFinished OnInitializationFinished;
	UPROPERTY(BlueprintReadOnly, Category = "VoxelLand|Initialization")
	bool bInitializing = false;
	UPROPERTY(BlueprintReadOnly, Category = "VoxelLand|Initialization")
	FString LastInitializationError;
	// Last completed camera-demand update. Nonzero means some requested chunks
	// could not be generated within the active M4 field's supported bounds.
	UPROPERTY(BlueprintReadOnly, Transient, Category = "VoxelLand|Streaming")
	int32 M4RejectedCameraDemandCount = 0;
	UPROPERTY(BlueprintReadOnly, Transient, Category = "VoxelLand|Streaming")
	FString M4FirstCameraDemandRejection;
	// Optional demonstration, started explicitly by a manager or test setup after BeginPlay.
	UFUNCTION(BlueprintCallable, Category = "VoxelLand|Demo")
	void StartDemo();
	UFUNCTION(BlueprintCallable, Category = "VoxelLand")
	void Shutdown();
	// Data-only explicit demand is useful on dedicated servers. Keep it until ReleaseChunk.
	bool RequestChunk(const FVoxelLandChunkKey& Key);
	void ReleaseChunk(const FVoxelLandChunkKey& Key);

	// Game-thread only. Each consumer owns its complete replacement set. Rejected updates keep the old lease.
	[[nodiscard]] bool UpdateCollisionDemand(const FGuid& ConsumerId, TConstArrayView<FVoxelLandChunkKey> Keys);
	// LocalBounds encloses the entire local sweep, including capsule dimensions and skin.
	[[nodiscard]] bool UpdateCollisionDemandForBounds(const FGuid& ConsumerId, const FBox& LocalBounds);
	void ReleaseCollisionDemand(const FGuid& ConsumerId);
	// Pending keys are absent from the immutable view and resolve to Missing when sampled.
	[[nodiscard]] bool CaptureCollisionView(const FGuid& ConsumerId, FVoxelLandCollisionWorldView& Out) const;
	[[nodiscard]] bool IsCollisionViewCurrent(const FVoxelLandCollisionWorldView& View) const;
	// Shared completed-field consumers use the same logical grid and admission rules.
	[[nodiscard]] bool GetTerrainPlacementGrid(double& OutChunkSizeCm, uint64& OutEpoch) const;
	[[nodiscard]] bool CollectTerrainPlacementKeys(const FBox& LocalBounds, TArray<FVoxelLandChunkKey>& Out) const;
	[[nodiscard]] bool GetTerminalTerrainPlacementError(FIntVector Coordinate, FString& OutError) const;

	// Chaos terrain is independent of the SDF and disabled unless explicitly enabled for this world.
	UFUNCTION(BlueprintCallable, Category = "VoxelLand|Chaos Collision")
	bool SetChaosTerrainCollisionEnabled(bool bEnabled);
	[[nodiscard]] bool ConfigureChaosTerrainCollision(
		const FVoxelLandChaosCollisionSettings& Settings, FString& OutError);
	[[nodiscard]] EVoxelLandChaosReadiness UpdateChaosCollisionLease(const FGuid& ConsumerId, const FBox& LocalBounds);
	[[nodiscard]] EVoxelLandChaosReadiness UpdateChaosCollisionTiles(
		const FGuid& ConsumerId, TConstArrayView<FVoxelLandChunkKey> Tiles);
	void ReleaseChaosCollisionLease(const FGuid& ConsumerId);
	EVoxelLandChaosReadiness GetChaosCollisionReadiness(const FGuid& ConsumerId) const;
	[[nodiscard]] bool CaptureChaosCollisionView(const FGuid& ConsumerId, FVoxelLandCollisionWorldView& Out) const;
	[[nodiscard]] bool SetChaosCollisionOverride(
		TConstArrayView<FVoxelLandChunkKey> Tiles, const FVoxelLandChaosCollisionOverride& Override);
	UFUNCTION(BlueprintCallable, Category = "VoxelLand|Chaos Collision")
	bool SetChaosCollisionTileOverride(FIntVector Coordinate, const FVoxelLandChaosCollisionOverride& Override);
	// A future authoritative update applies one complete batch per monotonically increasing revision.
	[[nodiscard]] bool ApplyChaosCollisionOverrideRevision(TConstArrayView<FVoxelLandChunkKey> Tiles,
		const FVoxelLandChaosCollisionOverride& Override, uint64 PolicyRevision);
	[[nodiscard]] bool GetChaosCollisionTileInfo(const FVoxelLandChunkKey& Key, FVoxelLandChaosTileInfo& Out) const;
	UFUNCTION(BlueprintCallable, Category = "VoxelLand|Chaos Collision")
	bool SetChaosCollisionBoundsOverride(const FBox& LocalBounds, const FVoxelLandChaosCollisionOverride& Override);
	[[nodiscard]] bool RequestChaosCollisionRebuild(TConstArrayView<FVoxelLandChunkKey> Tiles);
	UFUNCTION(BlueprintCallable, Category = "VoxelLand|Chaos Collision")
	bool RequestChaosCollisionRebuildForBounds(const FBox& LocalBounds);
	[[nodiscard]] bool NotifyChaosCollisionSourceChanged(
		TConstArrayView<FVoxelLandChunkKey> Tiles, uint64 SourceRevision);
	[[nodiscard]] bool RegisterChaosProp(
		UPrimitiveComponent& Component, FGuid& OutConsumerId, bool bSimulateWhenReady = true);
	void UnregisterChaosProp(const FGuid& ConsumerId);
	FVoxelLandChaosStats GetChaosCollisionStats() const;
#if WITH_DEV_AUTOMATION_TESTS
	void SetChaosCookHookForTests(FVoxelLandChaosCookHookForTests Hook);
	bool FailChaosFieldForTests(const FVoxelLandChunkKey& Key);
	bool ReplaceChaosFieldForTests(TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Field);
#endif
	UFUNCTION(BlueprintCallable, Category = "VoxelLand|Data")
	bool RequestDataChunk(FIntVector Coordinate);
	UFUNCTION(BlueprintCallable, Category = "VoxelLand|Data")
	void ReleaseDataChunk(FIntVector Coordinate);
	UFUNCTION(BlueprintPure, Category = "VoxelLand|Data")
	bool HasDataChunk(FIntVector Coordinate) const;
	// Returns the latest asynchronous failure for a still-resident chunk. A new
	// explicit request clears the failure and retries; releasing may retire it.
	UFUNCTION(BlueprintPure, Category = "VoxelLand|Data")
	bool GetDataChunkError(FIntVector Coordinate, FString& OutError) const;
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> FindChunk(const FVoxelLandChunkKey& Key) const;
	// A planar height clipmap uses Z=0 for the visibility of each XY terrain region.
	void SetRegionHidden(FIntVector Region, bool bRegionHidden);
	void SetTerrainMaterial(UMaterialInterface* Material);
	[[nodiscard]] bool SetMaterialWorldSignals(
		TSharedPtr<const class FVoxelLandWorldSignalSnapshot, ESPMode::ThreadSafe> Signals, FString& Error);
	UFUNCTION(BlueprintCallable, Category = "VoxelLand")
	void DumpStats() const;
#if WITH_DEV_AUTOMATION_TESTS
	void SetNextTestBarrier(TSharedPtr<FVoxelLandTestBarrier, ESPMode::ThreadSafe> Barrier);
	uint64 GetStaleCompletionCount() const;
	void SetTestViewLocation(FVector Location);
	void SetTestViewVelocity(FVector Velocity);
	int32 GetDemandedChunkCount() const;
	bool IsChunkDemanded(FIntVector Coordinate) const;
	bool HasWorldStateForTests() const { return State.IsValid(); }
	bool TryInitializeWithStartupForTests(
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator, const FVoxelLandChunkKey& Startup)
	{
		return TryInitialize(MoveTemp(Generator), &Startup);
	}
	bool IsExplicitChunkForTests(const FVoxelLandChunkKey& Key) const;
	uint64 GetM7PublishedTicketForTests() const;
	bool IsM7ReplacementPendingForTests() const;
	void CopyM7PublishedKeysForTests(TArray<FVoxelLandChunkKey>& Out) const;
	void SetFailWorkerStartForTests(bool bFail) { bFailWorkerStartForTests = bFail; }
	void SetPresentationHookForTests(TFunction<void()>&& Hook) { PresentationHookForTests = MoveTemp(Hook); }
	void SetShutdownHookForTests(TFunction<void()>&& Hook) { ShutdownHookForTests = MoveTemp(Hook); }
#if WITH_EDITOR
	void SetInitReloadBarrierForTests(TSharedPtr<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe> Barrier)
	{
		InitReloadBarrierForTests = MoveTemp(Barrier);
	}
	void OnInitObjectsReplacedForTests(const TMap<UObject*, UObject*>& Map) { HandleInitObjectsReplaced(Map); }
#endif
#endif

	// Optional data-only mode for a headless consumer or automation; dedicated servers always use it.
	UPROPERTY(EditAnywhere, Category = "VoxelLand|Streaming")
	bool bDataOnly = false;
	// Captured on successful initialization. Data-only worlds ignore visual settings.
	UPROPERTY(EditAnywhere, Category = "VoxelLand|Clipmap")
	bool bEnableM7Clipmap = false;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|Clipmap", meta = (ClampMin = "0", ClampMax = "5"))
	uint8 M7MaxLOD = 5;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|Clipmap", meta = (ClampMin = "1"))
	double M7HorizontalRadiusCm = 300000.0;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|Clipmap", meta = (ClampMin = "1"))
	double M7VerticalRadiusCm = 12800.0;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|Clipmap", meta = (ClampMin = "8", ClampMax = "1024"))
	int32 M7MaxLeaves = 512;
	UPROPERTY(BlueprintReadOnly, Transient, Category = "VoxelLand|Clipmap")
	FString M7ClipmapError;
	UPROPERTY(BlueprintReadOnly, Transient, Category = "VoxelLand|Clipmap")
	int32 M7PublishedLeafCount = 0;
	UPROPERTY(BlueprintReadOnly, Transient, Category = "VoxelLand|Clipmap")
	int32 M7FallbackRefinements = 0;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|Demo")
	int32 Seed = 1234;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|Demo", meta = (ClampMin = "1"))
	float VoxelSizeCm = 100.f;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|Streaming", meta = (ClampMin = "1", ClampMax = "8"))
	int32 HorizontalRadius = 4;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|Streaming", meta = (ClampMin = "1", ClampMax = "4"))
	int32 VerticalRadius = 2;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|Streaming", meta = (ClampMin = "1", ClampMax = "4"))
	int32 WorkerCount = 2;
	UPROPERTY(EditAnywhere, Category = "VoxelLand|Streaming", meta = (ClampMin = "1", ClampMax = "8192"))
	int32 MaxResidentChunks = 8192;
	// Horizontal region extents and vertical depth in chunks; applied on Initialize.
	UPROPERTY(EditAnywhere, Category = "VoxelLand|Streaming")
	FIntVector RegionSize = FIntVector(10, 10, 2);
	UPROPERTY(EditAnywhere, Category = "VoxelLand|Rendering")
	TObjectPtr<UMaterialInterface> TerrainMaterial;

private:
	bool InitializeDirectDensity(UVoxelLandWorldConfig* Settings, FIntVector StartupChunk);
	bool InitializeAuthoredM4(UVoxelLandWorldConfig* Settings, FIntVector StartupChunk);
	bool TryInitialize(TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator,
		const FVoxelLandChunkKey* Startup = nullptr, TArray<uint8> M6Identity = {},
		TSharedPtr<const struct FVoxelLandMaterialAppearance, ESPMode::ThreadSafe> Appearance = nullptr);
	bool ConfigureClipmap(FVoxelLandWorldState& Candidate);
	bool QueueStartupChunk(FVoxelLandWorldState& Candidate, const FVoxelLandChunkKey& Startup);
	bool StartTerrainWorkers(FVoxelLandWorldState& Candidate);
	static void StopCandidateWorkers(FVoxelLandWorldState& Candidate);
	void UpdateDemand();
	void UpdateM7Demand(const FVector& LogicalView);
	void DispatchM7Revision();
	void AcceptM7Completion(const VoxelLandWorldPrivate::FCompletion& Done);
	void AdvanceM7Publication();
	void DiscardM7Staging();
	void RetireChunk(const FVoxelLandChunkKey& Key);
	bool CanRequestChunk(const FVoxelLandChunkKey& Key, bool bLogRejection = false) const;
	bool CollectCollisionDemandKeys(const FBox& LocalBounds, TArray<FVoxelLandChunkKey>& Out) const;
	int32 ResolveChaosRenderLod(const FVoxelLandChunkKey& Key) const;
	void AdvanceChaosCollision();
	void StopChaosCollision(bool bDestroyBodies);
	friend class FVoxelLandChaosWorld;
	void PumpCompletions();
	void Dispatch();
	void HandleWorldBeginTearDown(UWorld* World);
	void BindWorldTearDown();
	void UnbindWorldTearDown();
	void ShutdownInternal(bool bDestroyRegionActors);
	void StopPendingInitialization();
#if WITH_EDITOR
	void HandleInitPackageReload(EPackageReloadPhase Phase, FPackageReloadedEvent* Event);
	void HandleInitObjectsReplaced(const TMap<UObject*, UObject*>& Map);
	void UnbindInitReloadHandlers();
#if WITH_DEV_AUTOMATION_TESTS
	TSharedPtr<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe> InitReloadBarrierForTests;
#endif
	FDelegateHandle InitPackageReloadHandle;
	FDelegateHandle InitObjectsReplacedHandle;
#endif
	void PollInitialization();
	void StopDemo();
	void PollDemo();
	TSharedPtr<FVoxelLandInitializationJob, ESPMode::ThreadSafe> PendingInitialization;
	TFuture<void> InitializationTask;
	TSharedPtr<FVoxelLandDemoJob, ESPMode::ThreadSafe> PendingDemo;
	TFuture<void> DemoTask;
	TSharedPtr<const FVoxelLandWorldSignalSnapshot, ESPMode::ThreadSafe> MaterialWorldSignals;
	UPROPERTY(Transient)
	TObjectPtr<UVoxelLandWorldConfig> MaterialWorldConfig;
	FGuid ActiveMaterialLayout;
	TArray<uint8> ActiveMaterialSourceIdentity;
	uint64 ConfigurationEpoch = 0;
	bool bLifecycleEnded = false;
	bool bIsEditorTerrainPreview = false;
#if WITH_EDITOR
	bool bIsEditorTerrainPreviewVisible = false;
	bool bHasEditorTerrainPreviewDemand = false;
	TArray<FVoxelLandChunkKey> EditorTerrainPreviewKeys;
#endif
	FDelegateHandle WorldTearDownHandle;
#if WITH_DEV_AUTOMATION_TESTS
	TFunction<void()> PresentationHookForTests;
	TFunction<void()> ShutdownHookForTests;
	bool bFailWorkerStartForTests = false;
#endif
	FVoxelLandChaosCollisionSettings ChaosCollisionSettings;
	TUniquePtr<FVoxelLandChaosWorld> ChaosCollision;
	TUniquePtr<FVoxelLandWorldState> State;
	UPROPERTY(Transient)
	TArray<TObjectPtr<AVoxelLandRegion>> Regions;
};
