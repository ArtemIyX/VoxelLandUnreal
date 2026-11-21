#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandError.h"
#include "Data/Structs/VoxelLandM4DomainSettings.h"
#include "Data/Structs/VoxelLandM4ErosionSettings.h"

class FVoxelLandM4HeightGrid;
class FVoxelLandM4GridLease;
struct FVoxelLandM4StageStats;

namespace VoxelLandM4Prepass
{
// Construct an immutable grid from caller-supplied, X-fastest Q8 base heights.
// When enabled, erosion starts derive from WorldSeed, StageId, Salt and each
// droplet index. This is a finite prepass only; it does not evaluate the graph,
// cache results or implement unlimited-world generation.
// OutputHash hashes versioned stage metadata and final little-endian samples;
// it is a result identity, not a compiler-proven dependency cache key.
// Both outputs are left unchanged on any error or cancellation.
VOXELLANDALGO_API FVoxelLandError Generate(const FVoxelLandM4DomainSettings& Domain,
	TConstArrayView<int32> BaseHeightsQ8, uint64 WorldSeed, const FGuid& StageId, uint32 Salt, int32 DropletCount,
	const FVoxelLandM4ErosionSettings& Erosion, TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid,
	uint64& OutHash, TFunctionRef<bool()> IsCancelled);
VOXELLANDALGO_API FVoxelLandError Generate(const FVoxelLandM4DomainSettings& Domain,
	TConstArrayView<int32> BaseHeightsQ8, uint64 WorldSeed, const FGuid& StageId, uint32 Salt, int32 DropletCount,
	const FVoxelLandM4ErosionSettings& Erosion, TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid,
	uint64& OutHash, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Lease);
// Internal composition overload: stats are copied into the published grid.
VOXELLANDALGO_API FVoxelLandError Generate(const FVoxelLandM4DomainSettings& Domain,
	TConstArrayView<int32> BaseHeightsQ8, uint64 WorldSeed, const FGuid& StageId, uint32 Salt, int32 DropletCount,
	const FVoxelLandM4ErosionSettings& Erosion, TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid,
	uint64& OutHash, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Lease, const FVoxelLandM4StageStats* StageStats);
}
