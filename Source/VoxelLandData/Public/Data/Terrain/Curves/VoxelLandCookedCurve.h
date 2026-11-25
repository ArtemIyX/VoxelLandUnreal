#pragma once

#include "CoreMinimal.h"
#include "Data/Terrain/Curves/VoxelLandCurveAuthoringKey.h"
#include "VoxelLandCookedCurve.generated.h"

// Versioned cooked LUT with exact authoring-key identity for stale detection.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM4CookedCurve
{
	GENERATED_BODY()
	UPROPERTY()
	int32 BakeVersion = 0;
	UPROPERTY()
	int64 SamplesHash = 0;
	UPROPERTY()
	TArray<FVoxelLandM4CurveAuthoringKey> SourceKeys;
	UPROPERTY()
	TArray<int32> SamplesQ8;
};
