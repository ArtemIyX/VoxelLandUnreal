#pragma once

#include "CoreMinimal.h"

struct FVoxelLandM4SavedWorld;
struct FVoxelLandM4WorldSnapshot;

namespace VoxelLandM4SavedWorld
{
// Worker thread only. Bounds the input first, then saves a value copy after checking its current control
// programs, provider dependencies, curves and density identity. No filesystem
// writes occur. Outputs are unchanged on failure.
VOXELLAND_API bool Capture(const FVoxelLandM4WorldSnapshot& Snapshot, FVoxelLandM4SavedWorld& Out, FString& Error);

// Worker thread only. Refuses unknown versions and mismatched or corrupt
// density inputs. The returned snapshot can be passed to M4 preparation subject
// to its usual admission, provider and cancellation checks. Runtime/provider
// versions must remain compatible; this is not an automatic migration policy.
VOXELLAND_API bool Restore(const FVoxelLandM4SavedWorld& Saved, FVoxelLandM4WorldSnapshot& Out, FString& Error);
}
