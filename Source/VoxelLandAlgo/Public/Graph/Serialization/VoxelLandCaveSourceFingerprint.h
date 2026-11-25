#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;
struct FVoxelLandGraphSnapshot;

namespace VoxelLand::M5::SourceFingerprint
{
// Bounded, UObject-free subset of the existing version-1 source encoding.
// Captured function dependencies and calls are supported with bounded ASCII paths.
// Non-ASCII names/paths, invalid dependency graphs and duplicate IDs are rejected.
// Caller retains the immutable input and its admission lease.
// This computes a source fingerprint only: it does not load a program, bind a
// provider or authorize identity capture. Output is unchanged on failure.
// Reservations cover writer and index payload estimates, not allocator/control
// block overhead, input ownership, the codec, or provider/registry work.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Compute(const FVoxelLandGraphSnapshot& Source,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	uint64& OutFingerprint);
}
