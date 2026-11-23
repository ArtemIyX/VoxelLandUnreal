#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

struct FVoxelLandM5CavePlan;
struct FVoxelLandM5PointCm;

// Pure post-filter sample processing. This is not a plan, roof, mesh, or
// walkability validator. The caller must provide the complete validated plan
// set for the sample box, including all influencing neighboring regions, and
// independently and conservatively derived network and pocket masks. An
// entrance may allow network carving without allowing a pocket in the roof.
// Mask bytes are exactly 0 or 1; protection overrides both permissions. Pocket
// values are Q8 distances. The original overload treats every pocket value,
// including zero, as present. Use the presence overload for PocketMapping.
// Samples and masks are X-fastest, then Y, then Z. Origin is the first stored
// sample, including any halo; spacing and voxel size are whole centimetres.
// MaxScratchBytes covers this function's result array, not its input/plan
// allocations. MaxWork bounds sample and per-plan primitive visits. All
// referenced inputs must stay immutable for the duration of this call.
namespace VoxelLand::M5::ChunkCarve
{
VOXELLANDALGO_API bool TryCarve(TConstArrayView<int16> Baseline, const FIntVector& Size,
	const FVoxelLandM5PointCm& OriginCm, int64 SpacingCm, int32 VoxelSizeCm, bool bNetworkEnabled, bool bPocketEnabled,
	TConstArrayView<TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe>> Plans,
	TConstArrayView<uint8> NetworkAllowed, TConstArrayView<uint8> PocketAllowed, TConstArrayView<uint8> Protected,
	TConstArrayView<int64> PocketDistanceQ8, int64 MaxScratchBytes, int64 MaxWork, TFunctionRef<bool()> IsCancelled,
	TArray<int16>& OutSamples);

// When pockets are enabled, PocketPresent has one binary byte per sample.
// A zero byte ignores its distance; a one byte composes it, including zero.
// Failure or cancellation leaves OutSamples unchanged.
VOXELLANDALGO_API bool TryCarve(TConstArrayView<int16> Baseline, const FIntVector& Size,
	const FVoxelLandM5PointCm& OriginCm, int64 SpacingCm, int32 VoxelSizeCm, bool bNetworkEnabled, bool bPocketEnabled,
	TConstArrayView<TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe>> Plans,
	TConstArrayView<uint8> NetworkAllowed, TConstArrayView<uint8> PocketAllowed, TConstArrayView<uint8> Protected,
	TConstArrayView<uint8> PocketPresent, TConstArrayView<int64> PocketDistanceQ8, int64 MaxScratchBytes, int64 MaxWork,
	TFunctionRef<bool()> IsCancelled, TArray<int16>& OutSamples);
}
