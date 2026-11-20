#include "Graph/VoxelLandPreviewViewport.h"
#include "AdvancedPreviewScene.h"
#include "EditorViewportClient.h"

void SVoxelLandPreviewViewport::Construct(const FArguments& Args)
{
    PreviewScene = Args._Scene;
    SEditorViewport::Construct(SEditorViewport::FArguments());
}
TSharedRef<FEditorViewportClient> SVoxelLandPreviewViewport::MakeEditorViewportClient()
{
    TSharedRef<FEditorViewportClient> ViewportClient = MakeShared<FEditorViewportClient>(nullptr, PreviewScene.Get(), SharedThis(this));
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
