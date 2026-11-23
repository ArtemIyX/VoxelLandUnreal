#pragma once

#include "CoreMinimal.h"
#include "Data/Enums/VoxelLandWorldKind.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;
class FVoxelLandOperatorRegistrySnapshot;
struct FVoxelLandWorldCommonSnapshot;

namespace VoxelLandWorldValidation
{
// Diagnostic validation only. Compiler, provider callbacks and program
// construction are not fully metered; success does not authorize publication,
// cooked reconstruction or runtime initialization.
VOXELLAND_API bool ValidateSnapshot(const FVoxelLandWorldCommonSnapshot& Snapshot, EVoxelLandWorldKind Kind,
	const FVoxelLandOperatorRegistrySnapshot& Providers, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, FString& OutError);
}
