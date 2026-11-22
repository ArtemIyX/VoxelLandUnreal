#pragma once
#include "CoreMinimal.h"
#include "Templates/Function.h"
class FVoxelLandM5Budget;
struct FVoxelLandM5Diagnostic;
struct FVoxelLandM4ControlSnapshot;
struct FVoxelLandZoneProfile;
namespace VoxelLandM4OverrideValidation
{
// Validate against declarations in the copied graph, including parameters that
// the compiler correctly omits as unreachable. No UObject access. Unknown,
// duplicated, mistyped and out-of-range overrides are errors.
VOXELLAND_API bool Validate(const FVoxelLandM4ControlSnapshot& Channel, FString& Error);
// Allocation-free validation of copied declarations/overrides; input storage
// and diagnostic allocator overhead remain the caller's responsibility.
VOXELLAND_API FVoxelLandM5Diagnostic ValidateMetered(const FVoxelLandM4ControlSnapshot& Channel,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
// Call after Validate; unreachable declarations have no compiled VM slot.
VOXELLAND_API void BuildProfile(
	const FVoxelLandM4ControlSnapshot& Channel, int32 VoxelSizeCm, FVoxelLandZoneProfile& Profile);
}
