#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandError.h"
#include "Data/Structs/VoxelLandM4StageFingerprintNode.h"
#include "Data/Structs/VoxelLandM4StageNode.h"
#include "Data/Structs/VoxelLandM4StageOutput.h"

namespace VoxelLandM4StageFingerprint
{
// Experimental format 1, scoped to a single named global output. First validates
// the entire classified graph, then canonically encodes only its upstream closure.
// Node semantics are caller-provided; the caller MUST encode ordered input pin
// bindings, transitive parameter/LUT/seed/provider dependencies and algorithm
// versions. Context must encode world-dependent inputs such as seed and domain.
// These bytes alone do not prove a complete cache key or validate providers.
// OutBytes and OutHash are unchanged on failure; retain/compare OutBytes when
// collision-free cache identity is required (xxHash64 is not collision-proof).
VOXELLANDALGO_API FVoxelLandError Build(TConstArrayView<FVoxelLandM4StageNode> Nodes,
	TConstArrayView<FVoxelLandM4StageOutput> Outputs, const FString& OutputName,
	TConstArrayView<FVoxelLandM4StageFingerprintNode> Semantics, TConstArrayView<uint8> Context,
	TArray<uint8>& OutBytes, uint64& OutHash);
}
