#pragma once

#include "CoreMinimal.h"

struct FVoxelLandM4WorldSnapshot;

namespace VoxelLandM4HeightStageValidation
{
// Validates immutable stage metadata and its three projected terminals.
VOXELLANDDATA_API bool Validate(const FVoxelLandM4WorldSnapshot& Snapshot, FString& OutError);
}
