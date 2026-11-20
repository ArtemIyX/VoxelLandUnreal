#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/Structs/VoxelLandChunkData.h"
#include "VoxelLandWorld.generated.h"

class FVoxelLandGenerator;
class UVoxelLandChunkComponent;
class UMaterialInterface;
struct FVoxelLandWorldState;

UCLASS(NotBlueprintable, Transient)
class VOXELLAND_API AVoxelLandRegion : public AActor
{
    GENERATED_BODY()
public:
    AVoxelLandRegion();
    void Publish(const FIntVector& Key, const FVector& Origin, TSharedPtr<const struct FVoxelLandMeshData,ESPMode::ThreadSafe> Mesh, UMaterialInterface* Material);
    void Remove(const FIntVector& Key);
    void SetTerrainMaterial(UMaterialInterface* Material);
    bool IsEmpty() const { return Components.IsEmpty(); }
private:
    UPROPERTY() TObjectPtr<USceneComponent> Root;
    UPROPERTY() TMap<FIntVector,TObjectPtr<UVoxelLandChunkComponent>> Components;
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
    // Generator snapshots must have been created on a worker thread. Replacing one invalidates prior jobs.
    void Initialize(TSharedPtr<const FVoxelLandGenerator,ESPMode::ThreadSafe> Generator);
    void Shutdown();
    // Data-only explicit demand is useful on dedicated servers. Keep it until ReleaseChunk.
    bool RequestChunk(const FVoxelLandChunkKey& Key);
    void ReleaseChunk(const FVoxelLandChunkKey& Key);
    TSharedPtr<const FVoxelLandChunkData,ESPMode::ThreadSafe> FindChunk(const FVoxelLandChunkKey& Key) const;
    void SetRegionHidden(FIntVector Region, bool bHidden);
    void SetTerrainMaterial(UMaterialInterface* Material);

    UPROPERTY(EditAnywhere, Category="VoxelLand|Demo") bool bStartDemo = true;
    UPROPERTY(EditAnywhere, Category="VoxelLand|Demo") int32 Seed = 1234;
    UPROPERTY(EditAnywhere, Category="VoxelLand|Demo", meta=(ClampMin="1")) float VoxelSizeCm = 100.f;
    UPROPERTY(EditAnywhere, Category="VoxelLand|Streaming", meta=(ClampMin="1",ClampMax="8")) int32 HorizontalRadius = 4;
    UPROPERTY(EditAnywhere, Category="VoxelLand|Streaming", meta=(ClampMin="1",ClampMax="4")) int32 VerticalRadius = 2;
    UPROPERTY(EditAnywhere, Category="VoxelLand|Streaming", meta=(ClampMin="1",ClampMax="4")) int32 WorkerCount = 2;
    UPROPERTY(EditAnywhere, Category="VoxelLand|Rendering") TObjectPtr<UMaterialInterface> TerrainMaterial;
private:
    void StartDemo();
    void UpdateDemand();
    void PumpCompletions();
    void Dispatch();
    uint64 ConfigurationEpoch = 0;
    TUniquePtr<FVoxelLandWorldState> State;
    UPROPERTY(Transient) TArray<TObjectPtr<AVoxelLandRegion>> Regions;
};
