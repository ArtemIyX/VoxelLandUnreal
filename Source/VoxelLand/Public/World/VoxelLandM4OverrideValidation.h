#pragma once
#include "CoreMinimal.h"
struct FVoxelLandM4ControlSnapshot;
namespace VoxelLandM4OverrideValidation
{
// Validate against declarations in the copied graph, including parameters that
// the compiler correctly omits as unreachable. No UObject access. Unknown,
// duplicated, mistyped and out-of-range overrides are errors.
VOXELLAND_API bool Validate(const FVoxelLandM4ControlSnapshot& Channel, FString& Error);
}
