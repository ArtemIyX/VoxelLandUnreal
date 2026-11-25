#pragma once

#include "CoreMinimal.h"
#include "VoxelLandHeightStageManifest.generated.h"

// Version 0 is the original world-config height stage. Version 1 represents
// the graph's three-input height terminal and the same fixed Q8 sum.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM4HeightStageManifest
{
	GENERATED_BODY()
	FVoxelLandM4HeightStageManifest();
	UPROPERTY()
	int32 Version = 0;
	UPROPERTY()
	FGuid OutputNodeId;
	UPROPERTY()
	FGuid ContinentalInputId;
	UPROPERTY()
	FGuid MountainInputId;
	UPROPERTY()
	FGuid PeaksInputId;
};
