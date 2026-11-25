#pragma once

#include "CoreMinimal.h"
#include "VoxelLandCaveStageManifest.generated.h"

// Pre-release contracts; a version is not a promise of generation support.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM5StageManifest
{
	GENERATED_BODY()
	UPROPERTY()
	int32 Descriptor = 1;
	UPROPERTY()
	int32 Seed = 1;
	UPROPERTY()
	int32 Topology = 1;
	UPROPERTY()
	int32 Geometry = 1;
	UPROPERTY()
	int32 RegionalOwnership = 1;
	UPROPERTY()
	int32 PocketNoise = 1;
	UPROPERTY()
	int32 SurfaceValidation = 1;
	UPROPERTY()
	// Version 2 protects zero-containing cells and complete roof sample dependencies.
	int32 Composition = 2;
};
