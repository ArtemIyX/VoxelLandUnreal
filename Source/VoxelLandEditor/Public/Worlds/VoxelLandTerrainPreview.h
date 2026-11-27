#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelLandTerrainPreview.generated.h"

class AVoxelLandWorld;
class UVoxelLandPCGComponent;
class UVoxelLandWorldConfig;

/** Authored in a normal level. Its working terrain and placement belong to the editor session. */
UCLASS(NotBlueprintable, meta = (DisplayName = "VoxelLand Terrain Preview"))
class VOXELLANDEDITOR_API AVoxelLandTerrainPreview final : public AActor
{
	GENERATED_BODY()

public:
	AVoxelLandTerrainPreview();

	bool IsEditorOnly() const override { return true; }
	bool ShouldTickIfViewportsOnly() const override;
	void Tick(float DeltaSeconds) override;

	UFUNCTION(CallInEditor, Category = "Terrain Preview")
	void GeneratePreview();

	UFUNCTION(CallInEditor, Category = "Terrain Preview")
	void RefreshPlacement();

	UFUNCTION(CallInEditor, Category = "Terrain Preview")
	void ClearPreview();

	AVoxelLandWorld* GetPreviewWorld() const { return VisibleWorld; }

	UPROPERTY(EditAnywhere, Category = "Terrain Preview")
	TObjectPtr<UVoxelLandWorldConfig> WorldConfig;

	UPROPERTY(EditAnywhere, Category = "Terrain Preview",
		meta = (ClampMin = "1.0", Units = "cm",
			ToolTip = "Half-size of the world-aligned work area around this actor."))
	FVector PreviewExtentCm = FVector(6400.0, 6400.0, 3200.0);

	UPROPERTY(EditAnywhere, Category = "Terrain Preview")
	bool bLiveRefresh = false;

	UPROPERTY(VisibleAnywhere, Category = "Terrain Preview")
	TObjectPtr<UVoxelLandPCGComponent> Placement;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Terrain Preview")
	FText PreviewStatus;

protected:
	void PostRegisterAllComponents() override;
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void PostEditMove(bool bFinished) override;
	void PostEditUndo() override;
	void Destroyed() override;
	void BeginDestroy() override;

private:
	static constexpr double RefreshDebounceSeconds = 0.25;

	void HandleObjectModified(UObject* Object);
	void HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Change);
	void UnbindAuthoringChanges();
	void QueueRefresh(bool bTerrain);
	void SetStatus(const FText& Text);
	void FailCandidate(const FText& Reason);
	void AdvanceCandidate();
	void RetireWorld(TObjectPtr<AVoxelLandWorld>& World);
	void ReleasePreview();
	bool CanPreview() const;

	UPROPERTY(Transient, DuplicateTransient)
	TObjectPtr<AVoxelLandWorld> VisibleWorld;

	UPROPERTY(Transient, DuplicateTransient)
	TObjectPtr<AVoxelLandWorld> PendingWorld;

	UPROPERTY(Transient, DuplicateTransient)
	TObjectPtr<UVoxelLandWorldConfig> VisibleConfig;

	UPROPERTY(Transient, DuplicateTransient)
	TObjectPtr<UVoxelLandWorldConfig> PendingConfig;

	FBox RequestedBounds = FBox(ForceInit);
	FBox PendingBounds = FBox(ForceInit);
	FBox VisibleBounds = FBox(ForceInit);
	FDelegateHandle ObjectModifiedHandle;
	FDelegateHandle ObjectPropertyChangedHandle;
	double RefreshTime = 0.0;
	bool bQueuedTerrainRefresh = false;
	bool bQueuedPlacementRefresh = false;
	bool bPendingDemand = false;
};
