#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;
class FVoxelLandProgram;
struct FVoxelLandGraphSnapshot;
struct FVoxelLandSerializedProgram;

// Loads the bounded, function-free, built-in-only subset of existing v1/v2
// payloads on a worker thread. Both source and program fingerprints must match.
// External providers/functions and non-ASCII names are rejected; nothing is
// recompiled or substituted. The returned program retains its admitted memory
// lease. Caller retains immutable input/provider ownership and accounts for
// allocator overhead. Failure or cancellation leaves OutProgram unchanged.
class VOXELLANDALGO_API FVoxelLandM5ProgramLoader final
{
public:
	static FVoxelLandM5Diagnostic Load(const FVoxelLandGraphSnapshot& Source,
		const FVoxelLandSerializedProgram& Payload, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram);
};
