#pragma once
#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "Data/Structs/VoxelLandMeshData.h"
#include "VoxelLandChunkComponent.generated.h"

UCLASS(ClassGroup=(VoxelLand), meta=(BlueprintSpawnableComponent))
class VOXELLANDRENDER_API UVoxelLandChunkComponent : public UPrimitiveComponent
{
    GENERATED_BODY()
public:
    UVoxelLandChunkComponent();
    // Must be called on the game thread. Invalid payloads leave the previous mesh intact.
    bool SetMesh(TSharedPtr<const FVoxelLandMeshData, ESPMode::ThreadSafe> InMesh);
    void ClearMesh();
    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
    virtual int32 GetNumMaterials() const override { return 1; }
    virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override;
    virtual void SetMaterial(int32 ElementIndex, UMaterialInterface* Material) override;
    virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
private:
    UPROPERTY() TObjectPtr<UMaterialInterface> TerrainMaterial;
    TSharedPtr<const FVoxelLandMeshData, ESPMode::ThreadSafe> Mesh;
};
