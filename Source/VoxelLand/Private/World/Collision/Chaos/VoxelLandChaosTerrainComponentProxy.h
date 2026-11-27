#pragma once

#if !UE_SERVER && !UE_BUILD_SHIPPING
#include "PrimitiveSceneProxy.h"

class UVoxelLandChaosTerrainComponent;
struct FVoxelLandChaosMesh;

class FVoxelLandChaosTerrainComponentProxy final : public FPrimitiveSceneProxy
{
public:
	FVoxelLandChaosTerrainComponentProxy(const UVoxelLandChaosTerrainComponent& Component,
		TSharedRef<const FVoxelLandChaosMesh, ESPMode::ThreadSafe> InMesh);

	virtual SIZE_T GetTypeHash() const override;
	virtual uint32 GetMemoryFootprint() const override;
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily,
		uint32 VisibilityMap, FMeshElementCollector& Collector) const override;

private:
	bool IsCollisionView(const FSceneViewFamily& ViewFamily) const;

	TSharedRef<const FVoxelLandChaosMesh, ESPMode::ThreadSafe> Mesh;
	bool bBlocksPawn;
	bool bBlocksVisibility;
};
#endif
