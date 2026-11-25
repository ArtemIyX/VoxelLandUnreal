#pragma once
#include "CoreMinimal.h"
#include "Templates/Function.h"
class FVoxelLandM5Budget;
struct FVoxelLandM5Diagnostic;
struct FVoxelLandM4ControlSnapshot;
namespace VoxelLandM4CurveValidation
{
// Validates the copied keys, bake version, content hash, and exact v1 integer
// rebake. Only bounded scratch data is used; no UObject or asset access.
VOXELLAND_API bool Validate(const FVoxelLandM4ControlSnapshot& Channel, FString& Error);
// Bounded native M5 path. The caller retains immutable input and accounts for
// its storage. Cancellation is checked before and after the legacy batch; it
// may be delayed by one bounded batch of up to 4097 keys and 4097 samples.
VOXELLAND_API FVoxelLandM5Diagnostic ValidateMetered(const FVoxelLandM4ControlSnapshot& Channel,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
}
