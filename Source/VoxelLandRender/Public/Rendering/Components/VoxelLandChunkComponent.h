#pragma once
#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "Data/Meshing/VoxelLandMeshData.h"
#include "Templates/Atomic.h"
#include "VoxelLandChunkComponent.generated.h"

#if WITH_EDITOR
class FVoxelLandPreviewUploadObservation;
#endif

UCLASS(ClassGroup = (VoxelLand), meta = (BlueprintSpawnableComponent))
class VOXELLANDRENDER_API UVoxelLandChunkComponent : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	UVoxelLandChunkComponent();
	// Must be called on the game thread. Invalid payloads leave the previous mesh intact.
	bool SetMesh(TSharedPtr<const FVoxelLandMeshData, ESPMode::ThreadSafe> InMesh);
	// Set before registration. A render-thread batch can change the state without rebuilding the proxy.
	void SetClipmapVisibilityState(TSharedPtr<TAtomic<bool>, ESPMode::ThreadSafe> State);
	static void CommitClipmapVisibility(TArray<TSharedPtr<TAtomic<bool>, ESPMode::ThreadSafe>> Show,
		TArray<TSharedPtr<TAtomic<bool>, ESPMode::ThreadSafe>> Hide);
#if WITH_EDITOR
	void SetPreviewUploadObservation(TSharedPtr<FVoxelLandPreviewUploadObservation, ESPMode::ThreadSafe> InObservation);
#endif
#if WITH_DEV_AUTOMATION_TESTS
	bool HasAmbientVisibilityForTests() const { return Mesh && !Mesh->AmbientVisibility.IsEmpty(); }
#endif
	void ClearMesh();
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual int32 GetNumMaterials() const override { return 1; }
	virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override;
	virtual void SetMaterial(int32 ElementIndex, UMaterialInterface* Material) override;
	virtual void GetUsedMaterials(
		TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;

private:
	UPROPERTY()
	TObjectPtr<UMaterialInterface> TerrainMaterial;
	TSharedPtr<const FVoxelLandMeshData, ESPMode::ThreadSafe> Mesh;
	TSharedPtr<TAtomic<bool>, ESPMode::ThreadSafe> ClipmapVisibilityState;
#if WITH_EDITOR
	TSharedPtr<FVoxelLandPreviewUploadObservation, ESPMode::ThreadSafe> PreviewUploadObservation;
#endif
};
