#pragma once

#include "Async/Future.h"
#include "Components/ActorComponent.h"
#include "VoxelLandPCGCellKey.h"
#include "VoxelLandPCGCellRecord.h"
#include "VoxelLandPCGDetachedCell.h"
#include "VoxelLandPCGLayer.h"
#include "VoxelLandPCGMaskCompile.h"

#include "VoxelLandPCGComponent.generated.h"

class AVoxelLandPCGCell;
class AVoxelLandWorld;
class UVoxelLandWorldConfig;
class UPCGGraphInterface;

UCLASS(ClassGroup = (VoxelLand), meta = (BlueprintSpawnableComponent))
class VOXELLANDPCG_API UVoxelLandPCGComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	UVoxelLandPCGComponent();

	UFUNCTION(BlueprintCallable, Category = "VoxelLand|PCG")
	void SetSourceWorld(AVoxelLandWorld* InWorld);

	UFUNCTION(BlueprintCallable, Category = "VoxelLand|PCG")
	void SetWorldConfig(UVoxelLandWorldConfig* InConfig);

	UFUNCTION(BlueprintCallable, Category = "VoxelLand|PCG")
	void RefreshPreview(const FBox& WorldBounds);

	UFUNCTION(BlueprintCallable, Category = "VoxelLand|PCG")
	void ClearPreview();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "VoxelLand|PCG")
	void RefreshPlacement();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelLand|PCG")
	TObjectPtr<AVoxelLandWorld> SourceWorld;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelLand|PCG")
	TObjectPtr<UVoxelLandWorldConfig> WorldConfig;

	UPROPERTY(EditAnywhere, Category = "VoxelLand|PCG")
	TArray<FVoxelLandPCGLayer> Layers;

	UPROPERTY(EditAnywhere, Category = "VoxelLand|PCG")
	bool bGenerateInGame = true;

	UPROPERTY(EditAnywhere, Category = "VoxelLand|PCG")
	bool bLiveRefresh = true;

	UPROPERTY(EditAnywhere, Category = "VoxelLand|PCG")
	bool bShowPreviewCandidatePoints = false;

	UPROPERTY(EditAnywhere, Category = "VoxelLand|PCG", meta = (ClampMin = "1", ClampMax = "512"))
	int32 MaxResidentCells = 128;

	UPROPERTY(EditAnywhere, Category = "VoxelLand|PCG", meta = (ClampMin = "1", ClampMax = "12"))
	int32 MaxCellJobs = 4;

	UPROPERTY(EditAnywhere, Category = "VoxelLand|PCG", meta = (ClampMin = "1", ClampMax = "128"))
	int32 MaxPendingFieldMiB = 32;

	UPROPERTY(EditAnywhere, Category = "VoxelLand|PCG", meta = (ClampMin = "1", ClampMax = "4096"))
	int32 MaxInstancesPerCell = 256;

	UPROPERTY(EditAnywhere, Category = "VoxelLand|PCG", meta = (ClampMin = "1", ClampMax = "4"))
	int32 CellChunks = 1;

	UPROPERTY(EditAnywhere, Category = "VoxelLand|PCG", meta = (ClampMin = "0", ClampMax = "4"))
	int32 VerticalCells = 1;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "VoxelLand|PCG")
	FString PlacementStatus;

	int32 GetVisibleCellCount() const;
	int32 GetVisibleInstanceCount() const;

protected:
	void BeginPlay() override;
	void OnUnregister() override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void InvalidatePlacement();
	void RetireAllCells();
	void StartMaskCompile();
	bool UpdateMask();
	void BuildDesiredCells(double CellSizeCm);
	void AddRegion(int32 Layer, const FBox& Bounds, const FVector& Focus, double CellSizeCm, bool bClip,
		TMap<FVoxelLandPCGCellKey, FBox>& Desired);
	void ProcessCells();
	bool AdvanceCell(const FVoxelLandPCGCellKey& Key, FVoxelLandPCGCellRecord& Record);
	bool StartCell(const FVoxelLandPCGCellKey& Key, FVoxelLandPCGCellRecord& Record, int64& PendingFieldBytes);
	void FinishCell(const FVoxelLandPCGCellKey& Key, FVoxelLandPCGCellRecord& Record);
	void ReleaseLease(FVoxelLandPCGCellRecord& Record);
	void CancelCell(FVoxelLandPCGCellRecord& Record);
	bool CanRun() const;
	AVoxelLandWorld* ResolveSourceWorld() const;
	UVoxelLandWorldConfig* ResolveWorldConfig() const;
	bool ValidateLayers();
	int32 GetCellSeed(const FVoxelLandPCGCellKey& Key) const;

#if WITH_EDITOR
	void OnAuthoringChanged(UObject* Object, FPropertyChangedEvent& Event);
	void BindAuthoredGraphs();
	void UnbindAuthoredGraphs();
	FDelegateHandle AuthoringChangedHandle;
	TArray<TWeakObjectPtr<UPCGGraphInterface>> BoundGraphs;
#endif

	TMap<FVoxelLandPCGCellKey, FVoxelLandPCGCellRecord> Cells;
	TArray<FVoxelLandPCGCellKey> CellOrder;
	TArray<FVoxelLandPCGDetachedCell> DetachedCells;
	FBox PreviewLogicalBounds = FBox(ForceInit);
	TFuture<FVoxelLandPCGMaskCompile> MaskCompile;
	TSharedPtr<const FVoxelLandMaskGraphProgram, ESPMode::ThreadSafe> MaskProgram;
	TSharedPtr<const IVoxelLandMaterialSignalSource, ESPMode::ThreadSafe> MaskSignals;
	UPROPERTY(Transient, DuplicateTransient)
	TWeakObjectPtr<AVoxelLandWorld> ActiveSourceWorld;

	UPROPERTY(Transient, DuplicateTransient)
	TWeakObjectPtr<UVoxelLandWorldConfig> ActiveWorldConfig;

	FString MaskError;
	uint64 Epoch = 0;
	double NextDemandUpdate = 0.0;
	double RefreshAt = 0.0;
	bool bPreview = false;
	bool bRefreshQueued = true;
	bool bLayersValid = false;
};
