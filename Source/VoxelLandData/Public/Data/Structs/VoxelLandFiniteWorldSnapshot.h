#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandWorldCommonSnapshot.h"
#include "Data/Structs/VoxelLandFiniteWorldSettings.h"
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
