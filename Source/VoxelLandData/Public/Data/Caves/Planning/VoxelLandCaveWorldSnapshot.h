#pragma once

#include "CoreMinimal.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Data/Caves/Planning/VoxelLandCaveSettings.h"
#include "Data/Caves/Persistence/VoxelLandCaveStageManifest.h"
#include "VoxelLandCaveWorldSnapshot.generated.h"

// UObject-free immutable-by-convention inputs, copied on the game thread.
// Runtime must independently validate providers, masks and the complete final plan.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM5WorldSnapshot
{
	GENERATED_BODY()
	UPROPERTY()
	int32 FormatVersion = 1;
	UPROPERTY()
	FVoxelLandM4WorldSnapshot Base;
	UPROPERTY()
	FVoxelLandM5StageManifest Manifest;
	UPROPERTY()
	FVoxelLandM5CaveSettings Caves;
};
