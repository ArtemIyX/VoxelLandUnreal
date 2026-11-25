#pragma once

#include "CoreMinimal.h"

struct FVoxelLandM4IdentityRecord;
struct FVoxelLandM4WorldSnapshot;
class FVoxelLandGenerator;
class FVoxelLandM4GridLease;

namespace VoxelLandM4WorldIdentity
{
// Worker-thread only. The caller must first capture settings on the game thread;
// each graph program is independently loaded from its copied source and payload.
// This evidence records exact density inputs but cannot reconstruct the original
// assets/settings. Keep those separately. Material and asset paths are excluded.
// Cross-version/platform compatibility is not claimed until backend versions are
// explicitly pinned. Outputs are unchanged on failure.
VOXELLAND_API bool Capture(const FVoxelLandM4WorldSnapshot& Snapshot, FVoxelLandM4IdentityRecord& Out, FString& Error);
// Only the current encoding, version 1, is accepted.
VOXELLAND_API bool CaptureVersion(
	const FVoxelLandM4WorldSnapshot& Snapshot, int32 Version, FVoxelLandM4IdentityRecord& Out, FString& Error);
// Recompute from fresh inputs and compare all bytes (never just the hash).
VOXELLAND_API bool Matches(
	const FVoxelLandM4IdentityRecord& Record, const FVoxelLandM4WorldSnapshot& Snapshot, FString& Error);
// Recreate from separately retained settings and graph assets only after exact
// identity comparison. Follow Prepare's admission and freshness rules.
VOXELLAND_API bool Regenerate(const FVoxelLandM4IdentityRecord& Record, const FVoxelLandM4WorldSnapshot& Snapshot,
	const TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>& Lease,
	const TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe>& ValidationLease,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator,
	FString& Error);
}
