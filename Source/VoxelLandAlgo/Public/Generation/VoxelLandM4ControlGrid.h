#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandError.h"

class FVoxelLandGenerator;
class FVoxelLandProgram;
class FVoxelLandWorkerContext;

/** Conservative opt-in bridge for evaluating one existing VM output as a
 * control map. The full validated bytecode is checked against a 2D-only
 * whitelist; any 3D opcode, LoadZ, filter, external provider or unknown opcode
 * is rejected, including instructions whose results might be unused. This is
 * not compiler-generated stage metadata, a named-output compiler, a cache key,
 * or a production global stage. Existing program semantics are unchanged.
 */
namespace VoxelLandM4ControlGrid
{
VOXELLANDALGO_API FVoxelLandError ValidateProgram(const FVoxelLandProgram& Program);

// Evaluate inclusive [0, CellsX] x [0, CellsY] on an integer-centimeter grid.
// Produces normalized uint16 samples in X-fastest row-major order from the VM's
// single StoreDensity output. Max 1024 cells per axis and 1,050,625 samples.
// Positions must be within +/- 2^24 cm, where every integer is exact in float;
// unsupported positions are rejected instead of rounded. This bound is not a
// world boundary. The caller must invoke this off the game thread, using its
// generator's worker exclusively on its creating thread. All output remains
// unchanged on error or cancellation; values are published after the final check.
VOXELLANDALGO_API FVoxelLandError Evaluate(const FVoxelLandGenerator& Generator, FVoxelLandWorkerContext& Worker,
	int64 OriginX, int64 OriginY, int64 SpacingCm, int32 CellsX, int32 CellsY, TArray<uint16>& OutSamples,
	TFunctionRef<bool()> IsCancelled);
}
