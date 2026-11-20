#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Async/Future.h"
#include "Data/Structs/VoxelLandChunkData.h"
#include "VoxelLandWorld.generated.h"

class FVoxelLandGenerator;
class UVoxelLandWorldSettings;
class UPrimitiveComponent;
class FVoxelLandInitializationJob;
class FVoxelLandDemoJob;
class FVoxelLandModule;
class UMaterialInterface;
#if WITH_EDITOR
class FPackageReloadedEvent;
enum class EPackageReloadPhase : uint8;
#endif
struct FVoxelLandWorldState;
struct FVoxelLandTestBarrier;

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
	virtual ~AVoxelLandWorld() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;
	virtual void BeginDestroy() override;
	// Generator snapshots must have been created on a worker thread. Replacing one invalidates prior jobs.
	void Initialize(TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator);
	// Copies settings on the game thread and initializes asynchronously. No work starts in BeginPlay.
	UFUNCTION(BlueprintCallable, Category = "VoxelLand|Initialization")
	bool InitializeFromSettings(UVoxelLandWorldSettings* Settings);
	UFUNCTION(BlueprintCallable, Category = "VoxelLand|Initialization")
	void CancelInitialization();
	UPROPERTY(BlueprintAssignable, Category = "VoxelLand|Initialization")
	FVoxelLandInitializationFinished OnInitializationFinished;
	UPROPERTY(BlueprintReadOnly, Category = "VoxelLand|Initialization")
	bool bInitializing = false;
	UPROPERTY(BlueprintReadOnly, Category = "VoxelLand|Initialization")
	FString LastInitializationError;
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
#if WITH_EDITOR
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
	void UpdateDemand();
	void RetireChunk(const FVoxelLandChunkKey& Key);
	void PumpCompletions();
	void Dispatch();
	void HandleWorldBeginTearDown(UWorld* World);
	void ShutdownInternal(bool bDestroyRegionActors);
	void StopPendingInitialization();
#if WITH_EDITOR
    void HandleInitPackageReload(EPackageReloadPhase Phase, FPackageReloadedEvent* Event);
    void HandleInitObjectsReplaced(const TMap<UObject*, UObject*>& Map);
    void UnbindInitReloadHandlers();
    FDelegateHandle InitPackageReloadHandle;
    FDelegateHandle InitObjectsReplacedHandle;
#endif
	void PollInitialization();
	void StopDemo();
	void PollDemo();
	friend class FVoxelLandModule;
	TSharedPtr<FVoxelLandInitializationJob, ESPMode::ThreadSafe> PendingInitialization;
	TFuture<void> InitializationTask;
	TSharedPtr<FVoxelLandDemoJob, ESPMode::ThreadSafe> PendingDemo;
	TFuture<void> DemoTask;
	uint64 ConfigurationEpoch = 0;
	bool bLifecycleEnded = false;
	FDelegateHandle WorldTearDownHandle;
	TUniquePtr<FVoxelLandWorldState> State;
	UPROPERTY(Transient)
	TArray<TObjectPtr<AVoxelLandRegion>> Regions;
};
