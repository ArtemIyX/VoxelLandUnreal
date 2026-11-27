#pragma once

#include "GameFramework/Actor.h"

#include "VoxelLandPCGCell.generated.h"

class UPCGComponent;
class UPCGGraphInterface;
class UPCGVoxelTerrainData;
class UPrimitiveComponent;
struct FVoxelLandPCGLayer;

UCLASS(NotPlaceable, Transient)
class VOXELLANDPCG_API AVoxelLandPCGCell final : public AActor
{
	GENERATED_BODY()

public:
	AVoxelLandPCGCell();

	const UPCGVoxelTerrainData* GetTerrainData() const { return TerrainData; }
	[[nodiscard]] bool Start(UPCGVoxelTerrainData* Snapshot, UPCGGraphInterface* Graph, int32 Seed);
	bool IsGenerating() const;
	bool Collect(TArray<FTransform>& Out, const FBox& Ownership, const FIntVector& CapturedOrigin, int32 MaxInstances);
	void StopExecution();
	void Publish(const FVoxelLandPCGLayer& Layer, const TArray<FTransform>& LogicalTransforms);
	int32 GetInstanceCount() const { return InstanceCount; }
#if WITH_EDITOR
	void RecordDebugPoints(TConstArrayView<FTransform> Output);
	int32 DrawDebugPoints(int32 MaxPoints) const;
#endif

private:
	UPROPERTY()
	TObjectPtr<UPCGComponent> Execution;

	UPROPERTY(Transient)
	TObjectPtr<UPCGVoxelTerrainData> TerrainData;

	UPROPERTY(Transient)
	TObjectPtr<UPrimitiveComponent> Instances;

	int32 InstanceCount = 0;
#if WITH_EDITOR
	TArray<FVector> DebugCandidates;
	TArray<FVector> DebugAccepted;
#endif
};
