#pragma once

#include "CoreMinimal.h"

/** Standalone M4 control-map and integer-coordinate numerical helpers.
 * These functions do not impose a world domain or enable a generation stage.
 */
namespace VoxelLandM4Control
{
// Clamp finite normalized values to [-1,1], then quantize to [0,65535]
// using floor((v+1)*32767.5+0.5). Output remains unchanged for NaN/Inf.
// VoxelLandAlgo uses precise floating-point semantics; cross-host identity
// near quantization boundaries still requires verification with real backends.
VOXELLANDALGO_API bool Quantize(float Value, uint16& OutValue);

// Convert an integer voxel index to world centimeters without floating point.
// VoxelSizeCm must be positive. Output is unchanged if multiplication overflows.
VOXELLANDALGO_API bool LatticePositionCm(int64 VoxelIndex, int64 VoxelSizeCm, int64& OutWorldCm);

// Map a world coordinate into an unbounded grid with mathematical floor division.
// SpacingCm must be positive. The remainder is in [0, SpacingCm), including
// for negative positions. Outputs are unchanged if subtraction overflows.
VOXELLANDALGO_API bool GridCoordinate(
	int64 WorldCm, int64 OriginCm, int64 SpacingCm, int64& OutIndex, int64& OutRemainderCm);
// Interpolate four Q8-centimeter corner heights over a square grid cell. The
// remainders must be in [0, SpacingCm]; a remainder equal to spacing selects
// the adjacent endpoint exactly. Height limits follow the proposed M4 range.
// Reject if exact signed-int64 intermediates cannot be proven safe; round once
// to nearest, ties away from zero. Output remains unchanged on failure. The
// caller supplies a valid in-domain cell and handles endpoint selection.
VOXELLANDALGO_API bool BilinearHeightQ8(int32 H00, int32 H10, int32 H01, int32 H11, int64 RemainderX, int64 RemainderY,
	int64 SpacingCm, int32& OutHeightQ8);
}