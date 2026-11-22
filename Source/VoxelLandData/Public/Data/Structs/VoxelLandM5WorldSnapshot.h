#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Data/Structs/VoxelLandM5CaveSettings.h"
#include "Data/Structs/VoxelLandM5StageManifest.h"
#include "VoxelLandM5WorldSnapshot.generated.h"

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
