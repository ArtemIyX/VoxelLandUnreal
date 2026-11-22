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
// Version 1 recreates legacy order-sensitive bytes; version 2 uses canonical
// override ordering; version 3 also binds each validated graph source hash;
// version 4 adds the graph-authored height-stage manifest; version 5 binds
// the opt-in fixed-lattice unlimited backend; version 6 binds its shaping contract.
// Useful for explicit migration and legacy verification.
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
