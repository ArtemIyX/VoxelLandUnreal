#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Async/Future.h"
#include "Data/Structs/VoxelLandChunkData.h"
#include "VoxelLandWorld.generated.h"

class FVoxelLandGenerator;
class UVoxelLandWorldSettings;
class UVoxelLandWorldDefinition;
class UVoxelLandM4WorldConfig;
class UVoxelLandM6WorldConfig;
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
struct FVoxelLandTestBarrier;
struct FVoxelLandReloadTestBarrier;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVoxelLandInitializationFinished, bool, bSuccess);

UCLASS(NotBlueprintable, Transient)
class VOXELLAND_API AVoxelLandRegion : public AActor
{
	GENERATED_BODY()
public:
	AVoxelLandRegion();
	bool Publish(const FIntVector& Key, const FVector& Origin,
		TSharedPtr<const struct FVoxelLandMeshData, ESPMode::ThreadSafe> Mesh, UMaterialInterface* Material);
	void Remove(const FIntVector& Key);
	void SetTerrainMaterial(UMaterialInterface* Material);
	bool IsEmpty() const { return Components.IsEmpty(); }
	bool IsChunkEffectivelyVisible(const FIntVector& Key) const;

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> Root;
	UPROPERTY()
	TMap<FIntVector, TObjectPtr<UPrimitiveComponent>> Components;
};

UCLASS(BlueprintType)
class VOXELLAND_API AVoxelLandWorld : public AActor
{
	GENERATED_BODY()
public:
	AVoxelLandWorld();
	AVoxelLandWorld(FVTableHelper& Helper);
	virtual ~AVoxelLandWorld() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;
	virtual void BeginDestroy() override;
	// Generator snapshots must have been created on a worker thread. Replacing one invalidates prior jobs.
	// A null generator leaves the current world and its work intact.
	void Initialize(TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator);
	// Copies settings on the game thread and initializes asynchronously. No work starts in BeginPlay.
	UFUNCTION(BlueprintCallable, Category = "VoxelLand|Initialization")
	bool InitializeFromSettings(UVoxelLandWorldSettings* Settings);
	// Initializes cave-disabled finite definitions asynchronously. The initial
	// chunk is selected near the control-grid center and its demand bubble must fit.
	// Enabled caves and other definitions remain rejected pending preparation.
	UFUNCTION(BlueprintCallable, Category = "VoxelLand|Initialization")
	bool InitializeFromWorldDefinition(UVoxelLandWorldDefinition* Definition);
	// Explicit M4 startup. The entire requested startup radius must fit the selected mode.
	// Admission reserves a conservative 128 MiB estimate per retained grid under
	// voxel.M4.PrepassBudgetMiB; actual allocator/backend usage is not measured.
	UFUNCTION(BlueprintCallable, Category = "VoxelLand|Initialization")
	bool InitializeM4FromSettings(UVoxelLandM4WorldConfig* Settings, FIntVector StartupChunk);
	// Explicit generation-time overlay. The complete startup footprint is validated asynchronously.
	UFUNCTION(BlueprintCallable, Category = "VoxelLand|Initialization")
	bool InitializeM6FromSettings(UVoxelLandM6WorldConfig* Settings, FIntVector StartupChunk);
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
	void SetRegionHidden(FIntVector Region, bool bRegionHidden);
	void SetTerrainMaterial(UMaterialInterface* Material);
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
	bool TryInitializeWithStartupForTests(TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator,
		const FVoxelLandChunkKey& Startup) { return TryInitialize(MoveTemp(Generator), &Startup); }
	bool IsExplicitChunkForTests(const FVoxelLandChunkKey& Key) const;
	void SetFailWorkerStartForTests(bool bFail) { bFailWorkerStartForTests = bFail; }
	void SetPresentationHookForTests(TFunction<void()>&& Hook) { PresentationHookForTests = MoveTemp(Hook); }
	void SetShutdownHookForTests(TFunction<void()>&& Hook) { ShutdownHookForTests = MoveTemp(Hook); }
#if WITH_EDITOR
    void SetInitReloadBarrierForTests(TSharedPtr<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe> Barrier) { InitReloadBarrierForTests = MoveTemp(Barrier); }
    void OnInitObjectsReplacedForTests(const TMap<UObject*, UObject*>& Map) { HandleInitObjectsReplaced(Map); }
#endif
#endif

	// Optional data-only mode for a headless consumer or automation; dedicated servers always use it.
	UPROPERTY(EditAnywhere, Category = "VoxelLand|Streaming")
	bool bDataOnly = false;
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
	bool InitializeAuthoredM4(UVoxelLandM4WorldConfig* Settings, FIntVector StartupChunk, UVoxelLandM6WorldConfig* M6);
	bool TryInitialize(TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator,
		const FVoxelLandChunkKey* Startup = nullptr, TArray<uint8> M6Identity = {});
	void UpdateDemand();
	void RetireChunk(const FVoxelLandChunkKey& Key);
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
	uint64 ConfigurationEpoch = 0;
	bool bLifecycleEnded = false;
	FDelegateHandle WorldTearDownHandle;
#if WITH_DEV_AUTOMATION_TESTS
	TFunction<void()> PresentationHookForTests;
	TFunction<void()> ShutdownHookForTests;
	bool bFailWorkerStartForTests = false;
#endif
	TUniquePtr<FVoxelLandWorldState> State;
	UPROPERTY(Transient)
	TArray<TObjectPtr<AVoxelLandRegion>> Regions;
};
