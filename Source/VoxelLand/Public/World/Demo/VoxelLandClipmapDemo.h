#pragma once

#include "Engine/TimerHandle.h"
#include "World/VoxelLandWorld.h"

#include "VoxelLandClipmapDemo.generated.h"

class UVoxelLandWorldConfig;

UCLASS(Placeable, Blueprintable, meta = (DisplayName = "VoxelLand Clipmap Demo"))
class VOXELLAND_API AVoxelLandClipmapDemo : public AVoxelLandWorld
{
	GENERATED_BODY()

public:
	AVoxelLandClipmapDemo();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(EditInstanceOnly, Category = "VoxelLand|Demo",
		meta = (DisplayName = "World Config",
			ToolTip =
				"Assign a VoxelLand World Config with Height Terrain and Infinite selected. Streams around the player camera in Play mode."))
	TObjectPtr<UVoxelLandWorldConfig> TerrainConfig;

	FTimerHandle StartupTimer;

	void StartTerrain();

	UFUNCTION()
	void HandleInitializationFinished(bool bSuccess);
};
