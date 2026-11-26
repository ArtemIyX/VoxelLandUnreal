#pragma once

#include "CoreMinimal.h"

/** Standalone proposed M4 slope-normalized density calculation, version 1.
 * This does not alter the existing HeightToDensity operation or its fingerprint.
 * All five heights and Z are signed Q8 centimeters and must lie in
 * [-512000000, 512000000]; the caller obtains the four neighboring final
 * heights at exactly +/- one voxel on each axis and checks domain margins.
 * VoxelSizeCm is an integral positive number no greater than 2000000.
 *
 * First compute each dimensionless central slope in Q12: round ties away
 * from zero ((Hplus - Hminus) * 4096 / (512 * VoxelSizeCm)). Each Q12
 * slope must have magnitude <= 1048576 (256). Compute the Q12 denominator
 * as the nearest integer square root of 4096^2 + Sx^2 + Sy^2; the exact
 * square-root midpoint can never be an integer. Finally compute Q8 density
 * by rounding (Z - Hcenter) * 4096 / denominator to nearest, ties away
 * from zero. All arithmetic is int64; these ranges bound the largest
 * intermediate below 2^63. There is no saturation. Negative density is
 * solid. Failure leaves OutDensityQ8 unchanged.
 *
 * This numerical contract is not a frozen world format and has not yet been
 * verified on multiple hosts. Use the existing M1 quantization after any
 * downstream density displacement and filters.
 */
namespace VoxelLandM4Slope
{
// Separate the XY stencil from Z so callers can retain the normalization per column.
bool TryGetNormalizationQ12(const FIntVector4& NeighborsQ8, int32 VoxelSizeCm, int64& OutNormalization);
int64 NormalizeDensityQ8(int64 UnnormalizedQ8, int64 NormalizationQ12);
VOXELLANDALGO_API bool DensityQ8(int64 ZQ8, int32 CenterQ8, int32 PlusXQ8, int32 MinusXQ8, int32 PlusYQ8,
	int32 MinusYQ8, int32 VoxelSizeCm, int64& OutDensityQ8);
}
