#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandGraphSnapshot.h"
#include "Data/Structs/VoxelLandSerializedProgram.h"
#include "Data/Structs/VoxelLandParameterOverride.h"
#include "Data/Structs/VoxelLandM4CurveAuthoringKey.h"
#include "Data/Structs/VoxelLandM4CookedCurve.h"
#include "VoxelLandM4ControlSnapshot.generated.h"

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
