#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandGraphSnapshot.h"
#include "Data/Structs/VoxelLandSerializedProgram.h"
#include "Data/Structs/VoxelLandParameterOverride.h"
#include "Data/Structs/VoxelLandM4CurveAuthoringKey.h"

// Game-thread capture; graph asset pointers are deliberately not retained.
struct FVoxelLandM4ControlSnapshot
{
	FVoxelLandGraphSnapshot Source;
	FVoxelLandSerializedProgram Payload;
	TArray<FVoxelLandParameterOverride> Overrides;
	TArray<FVoxelLandM4CurveAuthoringKey> CurveKeys;
};
