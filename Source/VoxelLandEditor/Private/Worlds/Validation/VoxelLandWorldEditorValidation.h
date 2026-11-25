#pragma once

#include "CoreMinimal.h"
#include "Data/World/Configuration/VoxelLandWorldKind.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;
class FVoxelLandOperatorRegistrySnapshot;
struct FVoxelLandWorldCommonSnapshot;
struct FVoxelLandGraphSnapshot;

namespace VoxelLandWorldEditorValidation
{
// A synchronous save waits for worker diagnostics. It must not dispatch
// provider code that could itself wait for the game thread. Until save
// diagnostics are asynchronous, only captured built-ins and function calls
// are permitted here; external operators remain editable but are deferred.
bool CanValidateSynchronously(const FVoxelLandWorldCommonSnapshot& Snapshot, FString& OutError);
bool CanValidateSynchronously(const FVoxelLandGraphSnapshot& Snapshot, FString& OutError);

// Checks the captured source graphs against one frozen provider registry and
// validates their curve inputs. This is an editor diagnostic only: it does not
// produce cooked data or authorize runtime initialization.
bool ValidateSnapshot(const FVoxelLandWorldCommonSnapshot& Snapshot, EVoxelLandWorldKind Kind,
	const FVoxelLandOperatorRegistrySnapshot& Providers, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, FString& OutError);
}
