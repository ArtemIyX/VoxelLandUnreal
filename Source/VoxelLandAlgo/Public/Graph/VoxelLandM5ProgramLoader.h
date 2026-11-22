#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;
class FVoxelLandProgram;
struct FVoxelLandGraphSnapshot;
struct FVoxelLandSerializedProgram;

// Loads the bounded, built-in-only subset of current v2 payloads on a worker
// thread. Captured zero-input functions are supported; functions with interface
// inputs, external providers and non-ASCII names remain unsupported. Both source
// and program fingerprints must match. Nothing is recompiled or substituted.
// The returned program retains its admitted memory
// lease. Caller retains immutable input/provider ownership and accounts for
// allocator overhead. Failure or cancellation leaves OutProgram unchanged.
class VOXELLANDALGO_API FVoxelLandM5ProgramLoader final
{
public:
	static FVoxelLandM5Diagnostic Load(const FVoxelLandGraphSnapshot& Source,
		const FVoxelLandSerializedProgram& Payload, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
		TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram);
};
