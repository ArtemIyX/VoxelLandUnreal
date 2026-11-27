#pragma once

#include "World/VoxelLandWorld.h"

#include "VoxelLandPCGWorld.generated.h"

class UVoxelLandPCGComponent;
class UVoxelLandWorldConfig;

UCLASS(DisplayName = "VoxelLand PCG World")
class VOXELLANDPCG_API AVoxelLandPCGWorld final : public AVoxelLandWorld
{
	GENERATED_BODY()

public:
	AVoxelLandPCGWorld();

	UPROPERTY(EditAnywhere, Category = "VoxelLand|Startup")
	TObjectPtr<UVoxelLandWorldConfig> Config;

	UPROPERTY(EditAnywhere, Category = "VoxelLand|Startup")
	FIntVector StartupChunk = FIntVector::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VoxelLand|PCG")
	TObjectPtr<UVoxelLandPCGComponent> Placement;

protected:
	void BeginPlay() override;
	void EndPlay(EEndPlayReason::Type EndPlayReason) override;

private:
	void StartTerrain();
	FTimerHandle StartupTimer;
};
