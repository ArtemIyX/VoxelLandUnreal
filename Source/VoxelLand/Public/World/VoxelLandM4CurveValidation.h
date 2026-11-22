#pragma once
#include "CoreMinimal.h"
struct FVoxelLandM4ControlSnapshot;
namespace VoxelLandM4CurveValidation
{
// Validates the copied keys, bake version, content hash, and exact v1 integer
// rebake. Only bounded scratch data is used; no UObject or asset access.
VOXELLAND_API bool Validate(const FVoxelLandM4ControlSnapshot& Channel, FString& Error);
}
