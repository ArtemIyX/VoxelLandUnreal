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
    TSharedRef<FEditorViewportClient> Client = MakeShared<FEditorViewportClient>(nullptr, PreviewScene.Get(), SharedThis(this));
    Client->SetViewLocation(FVector(-7000.0, -7000.0, 5000.0));
    Client->SetViewRotation(FRotator(-25.0, 45.0, 0.0));
    Client->SetViewMode(VMI_Lit);
    Client->SetRealtime(true);
    return Client;
}
void SVoxelLandPreviewViewport::ResetCamera()
{
    if (TSharedPtr<FEditorViewportClient> Client = GetViewportClient())
    {
        Client->SetViewLocation(FVector(-7000.0, -7000.0, 5000.0));
        Client->SetViewRotation(FRotator(-25.0, 45.0, 0.0));
        Invalidate();
    }
}
