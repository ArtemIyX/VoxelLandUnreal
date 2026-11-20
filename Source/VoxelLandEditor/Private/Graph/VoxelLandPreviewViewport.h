#pragma once
#include "CoreMinimal.h"
#include "SEditorViewport.h"

class FAdvancedPreviewScene;
class SVoxelLandPreviewViewport : public SEditorViewport
{
public:
    SLATE_BEGIN_ARGS(SVoxelLandPreviewViewport) {}
        SLATE_ARGUMENT(TSharedPtr<FAdvancedPreviewScene>, Scene)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    void ResetCamera();
    void SetTerrainBounds(const FBox& Bounds);
    bool FrameTerrain();
    FBox GetTerrainBounds() const { return TerrainBounds; }
protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
private:
    TSharedPtr<FAdvancedPreviewScene> PreviewScene;
    FBox TerrainBounds = FBox(ForceInit);
};
