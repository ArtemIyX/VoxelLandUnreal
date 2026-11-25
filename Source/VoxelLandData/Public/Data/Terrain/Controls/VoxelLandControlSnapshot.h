#pragma once

#include "CoreMinimal.h"
#include "Data/Graph/Sources/VoxelLandGraphSnapshot.h"
#include "Data/Program/Serialization/VoxelLandSerializedProgram.h"
#include "Data/Graph/Sources/VoxelLandParameterOverride.h"
#include "Data/Terrain/Curves/VoxelLandCurveAuthoringKey.h"
#include "Data/Terrain/Curves/VoxelLandCookedCurve.h"
#include "VoxelLandControlSnapshot.generated.h"

// Game-thread capture; graph asset pointers are deliberately not retained.
USTRUCT()
struct VOXELLANDDATA_API FVoxelLandM4ControlSnapshot
{
	GENERATED_BODY()
	UPROPERTY()
	FVoxelLandGraphSnapshot Source;
	UPROPERTY()
	FVoxelLandSerializedProgram Payload;
	UPROPERTY()
	TArray<FVoxelLandParameterOverride> Overrides;
	UPROPERTY()
	TArray<FVoxelLandM4CurveAuthoringKey> CurveKeys;
	UPROPERTY()
	FVoxelLandM4CookedCurve CookedCurve;
};
