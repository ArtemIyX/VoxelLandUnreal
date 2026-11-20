#pragma once

#include "AdvancedPreviewScene.h"

class UStaticMeshComponent;

// The shared asset-viewer profile may re-enable its floor, so the terrain
// preview removes that component from its scene without modifying the profile.
class FVoxelLandPreviewScene final : public FAdvancedPreviewScene
{
public:
    FVoxelLandPreviewScene();
    bool IsBackdropRegistered() const;
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
private:
    TObjectPtr<UStaticMeshComponent> RemovedFloor;
    TObjectPtr<UStaticMeshComponent> RemovedEnvironment;
};
