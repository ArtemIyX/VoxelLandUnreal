#pragma once

#include "CoreMinimal.h"

struct FVoxelLandM5WorldSnapshot;
struct FVoxelLandM5IdentityRecord;

// Pre-release identity for opt-in M5 inputs only. Does not establish that cave
// planning, provider retention, cooking or reconstruction is implemented.
// Pocket threshold is encoded as unsigned [0,65535]; contract version 1 maps
// it to the signed sampler by subtracting 32768 before the strict comparison.
// Worker thread only: Capture also validates the complete M4 program inputs.
// Capture leaves Out unchanged on failure; Matches compares complete bytes.
namespace VoxelLandM5WorldIdentity
{
VOXELLAND_API bool Capture(const FVoxelLandM5WorldSnapshot& Snapshot, FVoxelLandM5IdentityRecord& Out, FString& Error);
VOXELLAND_API bool Matches(
	const FVoxelLandM5IdentityRecord& Record, const FVoxelLandM5WorldSnapshot& Snapshot, FString& Error);
}
