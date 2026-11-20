#include "Graph/VoxelLandPreviewViewport.h"
#include "AdvancedPreviewScene.h"
#include "EditorViewportClient.h"
#include "InputCoreTypes.h"

namespace
{
class FVoxelLandPreviewViewportClient final : public FEditorViewportClient
{
public:
    FVoxelLandPreviewViewportClient(FAdvancedPreviewScene* Scene, const TSharedRef<SVoxelLandPreviewViewport>& Widget)
        : FEditorViewportClient(nullptr, Scene, Widget), PreviewWidget(Widget) {}
    virtual bool InputKey(const FInputKeyEventArgs& Args) override
    {
        if (Args.Key == EKeys::F && Args.Event == IE_Pressed)
        {
            if (TSharedPtr<SVoxelLandPreviewViewport> PinnedPreview = PreviewWidget.Pin())
            {
                PinnedPreview->FrameTerrain();
                return true;
            }
        }
        return FEditorViewportClient::InputKey(Args);
    }
private:
    TWeakPtr<SVoxelLandPreviewViewport> PreviewWidget;
};
}

void SVoxelLandPreviewViewport::Construct(const FArguments& Args)
{
    PreviewScene = Args._Scene;
    SEditorViewport::Construct(SEditorViewport::FArguments());
}
TSharedRef<FEditorViewportClient> SVoxelLandPreviewViewport::MakeEditorViewportClient()
{
    TSharedRef<FEditorViewportClient> ViewportClient = MakeShared<FVoxelLandPreviewViewportClient>(PreviewScene.Get(), SharedThis(this));
    ViewportClient->SetViewLocation(FVector(-7000.0, -7000.0, 5000.0));
    ViewportClient->SetViewRotation(FRotator(-25.0, 45.0, 0.0));
    ViewportClient->SetViewMode(VMI_Lit);
    ViewportClient->SetRealtime(true);
    return ViewportClient;
}
void SVoxelLandPreviewViewport::ResetCamera()
{
    if (TSharedPtr<FEditorViewportClient> ViewportClient = GetViewportClient())
    {
        ViewportClient->SetViewLocation(FVector(-7000.0, -7000.0, 5000.0));
        ViewportClient->SetViewRotation(FRotator(-25.0, 45.0, 0.0));
        Invalidate();
    }
}

void SVoxelLandPreviewViewport::SetTerrainBounds(const FBox& Bounds)
{
    TerrainBounds = Bounds;
}
bool SVoxelLandPreviewViewport::FrameTerrain()
{
    if (!TerrainBounds.IsValid || TerrainBounds.Min.ContainsNaN() || TerrainBounds.Max.ContainsNaN())
    {
        return false;
    }
    if (TSharedPtr<FEditorViewportClient> ViewportClient = GetViewportClient())
    {
        if (!ViewportClient->Viewport)
        {
            return false;
        }
        const double Padding = TerrainBounds.GetExtent().Size() * 0.5;
        ViewportClient->FocusViewportOnBox(TerrainBounds.ExpandBy(Padding), true);
        Invalidate();
        return true;
    }
    return false;
}
