#include "VoxelLandPreviewScene.h"
#include "Components/StaticMeshComponent.h"

FVoxelLandPreviewScene::FVoxelLandPreviewScene()
	: FAdvancedPreviewScene(FPreviewScene::ConstructionValues())
{
	RemovedFloor = const_cast<UStaticMeshComponent*>(GetFloorMeshComponent());
	RemoveComponent(RemovedFloor);
	// The asset-viewer sky sphere includes a photographed ground plane that can
	// obscure large terrain previews. Keep the sky light, but remove the backdrop.
	RemovedEnvironment = SkyComponent;
	RemoveComponent(RemovedEnvironment);
	SetLightDirection(FRotator(-35.0, 125.0, 0.0));
	SetLightBrightness(0.85f);
	SetSkyBrightness(0.18f);
}

void FVoxelLandPreviewScene::AddReferencedObjects(FReferenceCollector& Collector)
{
	FAdvancedPreviewScene::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(RemovedFloor);
	Collector.AddReferencedObject(RemovedEnvironment);
}

bool FVoxelLandPreviewScene::IsBackdropRegistered() const
{
	return RemovedEnvironment && RemovedEnvironment->IsRegistered();
}