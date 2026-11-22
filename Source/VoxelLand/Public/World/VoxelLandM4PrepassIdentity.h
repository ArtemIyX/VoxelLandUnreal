#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandError.h"

class FVoxelLandProgram;
struct FVoxelLandM4WorldSnapshot;

namespace VoxelLandM4PrepassIdentity
{
// Versioned, exact candidate bytes for this process's current finite prepass
// implementation. The three programs must already have been loaded and validated
// independently against the current source, cooked payload, and provider registry.
// No cache lookup or reuse is enabled by this API. Callers must additionally
// establish coalescing, producer cancellation, lifetime and budget invariants.
// All control bytecode is upstream in the present single-output 2D VM; therefore
// the entire validated control program is encoded. Downstream smoothing,
// overhang, presentation, camera and material settings are deliberately omitted.
// Compare the complete byte array, never a hash alone. OutBytes is unchanged on
// failure. Version 1 preserves authored override order for existing world
// identities; version 2 sorts unique override IDs; version 3 binds the graph height-stage version. The bytes are process-local until all algorithm/backend versions are
// explicitly bound to a persistent world format.
VOXELLAND_API FVoxelLandError BuildCandidate(const FVoxelLandM4WorldSnapshot& Snapshot,
	const FVoxelLandProgram& Continental, const FVoxelLandProgram& Mountain, const FVoxelLandProgram& Peaks,
	TArray<uint8>& OutBytes, uint32 EncodingVersion = 2);
}
