#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"

struct FVoxelLandM4ControlPipelineInputs;
class FVoxelLandM4GridLease;
class FVoxelLandM4HeightGrid;
class FVoxelLandM5Budget;

// Partial, synchronous accounting for one finite M4 control pipeline. Inputs,
// validated programs, generator workers, native noise/provider resources and
// allocator overhead remain the caller's independently admitted responsibility.
// The M4 lease is retained but is not proof of its own admission provenance.
// The grid retains a wrapper, so RetainsLease(original process lease) is false.
// This result is not a validated descriptor, bound base or cave certificate.
namespace VoxelLand::M5::MeteredControlPipeline
{
// Charges bounded logical instruction/sample and erosion visits before the
// producer starts. Reserves conservative logical array capacities; these are
// not measurements of process memory or CPU time. Cancellation or any failure
// preserves OutGrid and OutHash. The retained grid owns its budget reservation
// and process lease through its final reader; temporary admission lasts through
// the entire synchronous producer call. Inputs remain immutable throughout.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Generate(const FVoxelLandM4ControlPipelineInputs& Inputs,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> ProcessLease, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& OutGrid, uint64& OutHash);
}
