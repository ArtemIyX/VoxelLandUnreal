#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Data/Structs/VoxelLandM5PointCm.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

class FVoxelLandM5Budget;

namespace VoxelLand::M5::SurfaceQuery
{
// Deliberately narrow sufficient condition for an exactly horizontal baseline
// surface. Samples must be the actual immutable post-filter, quantized M4 field
// on an X-fastest regular lattice; Origin is the global position of sample 0.
// Origin must be voxel-aligned; spacing is an even integer in [2,65536] cm
// and all grid endpoints lie in the signed +/-1048576 cm numeric envelope.
// ProtectionColumns is an X-fastest X*Y array of conservative binary flags.
// The entire requested square footprint and two extra samples on each side
// must be available. Each inspected column must have one strict solid-to-air
// crossing, at the same Z pair and with identical opposite-valued samples.
// Other surfaces, including zero-valued samples and slopes, are Unproven.
// Caller must prove the field/protection provenance, complete Z domain,
// compatible Surface Nets mesher precision and neighbor ownership, and apply
// additional filter, roof, carve-influence and validation halos as required.
// Caller retains immutable input storage and its memory-admission lease for the
// entire call; this borrowed-view API cannot verify lease or budget provenance.
// It allocates no sample-grid scratch; diagnostics may allocate a small string.
// One work unit per sample, three per column and one fixed unit are precharged;
// failed charges preserve outputs. This counter is not a CPU-time measurement.
// This does not validate entrance usability, clearance, roof, or water input.
// No output is modified on failure or cancellation.
VOXELLANDALGO_API FVoxelLandM5Diagnostic QueryFlatSurface(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	FIntVector SampleCounts, const FVoxelLandM5PointCm& Origin, int64 SpacingCm, TConstArrayView<int16> Samples,
	TConstArrayView<uint8> ProtectionColumns, int64 X, int64 Y, int32 RadiusCm, bool bHasWaterLevel, int64 WaterLevelCm,
	int64 MaxSamplesToInspect, TFunctionRef<bool()> IsCancelled, int64& OutSurfaceZ, int64& OutMinSurfaceZ,
	int64& OutMaxSurfaceZ, int32& OutSlopeDegrees, bool& bOutProtected, bool& bOutFlooded);
}
