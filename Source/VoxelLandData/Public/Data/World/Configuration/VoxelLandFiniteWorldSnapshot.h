#pragma once

#include "CoreMinimal.h"
#include "Data/World/Configuration/VoxelLandWorldCommonSnapshot.h"
#include "Data/World/Configuration/VoxelLandFiniteWorldSettings.h"
#include "VoxelLandFiniteWorldSnapshot.generated.h"

// Finite-world worker inputs; contains no asset pointers.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandFiniteWorldSnapshot
{
	GENERATED_BODY()
	UPROPERTY()
	FVoxelLandWorldCommonSnapshot Common;
	UPROPERTY()
	FVoxelLandFiniteWorldSettings Finite;
};
