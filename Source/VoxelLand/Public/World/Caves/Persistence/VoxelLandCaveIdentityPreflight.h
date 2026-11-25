#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;
struct FVoxelLandM5WorldSnapshot;
struct FVoxelLandM5IdentityPreflightLimits;
struct FVoxelLandM5IdentityPreflightStats;

// Read-only inventory of variable-sized M4 graph/program inputs. This does NOT
// authorize identity capture or preparation: registry snapshots, FText/soft
// path storage, codec copies/sorting, provider callbacks and actual allocation
// capacity remain unmeasured. Caller retains an immutable native snapshot.
// Charges bounded traversal work before inspecting each record. Output is
// unchanged on all errors or cancellation; consumed work is not refunded.
namespace VoxelLand::M5::IdentityPreflight
{
VOXELLAND_API FVoxelLandM5Diagnostic Inspect(const FVoxelLandM5WorldSnapshot& Snapshot,
	const FVoxelLandM5IdentityPreflightLimits& Limits, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, FVoxelLandM5IdentityPreflightStats& OutStats);
}
