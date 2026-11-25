#pragma once

#include "CoreMinimal.h"
#include "Data/World/Configuration/VoxelLandWorldCommonSnapshot.h"
#include "Data/World/Configuration/VoxelLandInfiniteWorldSettings.h"
#include "VoxelLandInfiniteWorldSnapshot.generated.h"

// Infinite-style world worker inputs; no whole-world bounds or asset pointers.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandInfiniteWorldSnapshot
{
	GENERATED_BODY()
	UPROPERTY()
	FVoxelLandWorldCommonSnapshot Common;
	UPROPERTY()
	FVoxelLandInfiniteWorldSettings Infinite;
};
