#include "VoxelLandPreviewViewport.h"
#include "VoxelLandInspection.h"
#include "AdvancedPreviewScene.h"
#include "EditorViewportClient.h"
#include "InputCoreTypes.h"
#include "UnrealClient.h"

class FVoxelLandPreviewViewportInternals final
{
public:
	class FVoxelLandPreviewViewportClient final : public FEditorViewportClient
	{
	public:
		FVoxelLandPreviewViewportClient(
			FAdvancedPreviewScene* Scene, const TSharedRef<SVoxelLandPreviewViewport>& Widget)
			: FEditorViewportClient(nullptr, Scene, Widget)
			, PreviewWidget(Widget)
		{
		}

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
};

void SVoxelLandPreviewViewport::Construct(const FArguments& Args)
{
	PreviewScene = Args._Scene;
	SEditorViewport::Construct(SEditorViewport::FArguments());
}

TSharedRef<FEditorViewportClient> SVoxelLandPreviewViewport::MakeEditorViewportClient()
{
	TSharedRef<FEditorViewportClient> ViewportClient =
		MakeShared<FVoxelLandPreviewViewportInternals::FVoxelLandPreviewViewportClient>(
			PreviewScene.Get(), SharedThis(this));
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
		if (PreviousViewFov.IsSet())
		{
			ViewportClient->ViewFOV = PreviousViewFov.GetValue();
			PreviousViewFov.Reset();
		}
		if (PreviousStoredFov.IsSet())
		{
			ViewportClient->FOVAngle = PreviousStoredFov.GetValue();
			PreviousStoredFov.Reset();
		}
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

bool SVoxelLandPreviewViewport::SetInspectionCamera(const FVector& Target)
{
	if (Target.ContainsNaN())
	{
		return false;
	}
	if (TSharedPtr<FEditorViewportClient> ViewportClient = GetViewportClient())
	{
		if (!ViewportClient->Viewport)
		{
			return false;
		}
		if (!PreviousViewFov.IsSet())
		{
			PreviousViewFov = ViewportClient->ViewFOV;
		}
		if (!PreviousStoredFov.IsSet())
		{
			PreviousStoredFov = ViewportClient->FOVAngle;
		}
		const FVector Location = VoxelLandM4Inspection::CameraLocation(Target);
		ViewportClient->SetViewLocation(Location);
		ViewportClient->SetViewRotation((Target - Location).Rotation());
		ViewportClient->ViewFOV = VoxelLandM4Inspection::HorizontalFovDegrees;
		ViewportClient->FOVAngle = VoxelLandM4Inspection::HorizontalFovDegrees;
		Invalidate();
		return true;
	}
	return false;
}

bool SVoxelLandPreviewViewport::GetCameraDetails(
	FVector& OutLocation, FRotator& OutRotation, float& OutHorizontalFov, FIntPoint& OutResolution) const
{
	if (TSharedPtr<FEditorViewportClient> ViewportClient = GetViewportClient())
	{
		if (!ViewportClient->Viewport)
		{
			return false;
		}
		OutLocation = ViewportClient->GetViewLocation();
		OutRotation = ViewportClient->GetViewRotation();
		OutHorizontalFov = ViewportClient->ViewFOV;
		OutResolution = ViewportClient->Viewport->GetSizeXY();
		return OutResolution.X > 0 && OutResolution.Y > 0;
	}
	return false;
}
