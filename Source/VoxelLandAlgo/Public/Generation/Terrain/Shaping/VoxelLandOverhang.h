#pragma once

#include "CoreMinimal.h"
#include "Data/Terrain/Shaping/VoxelLandOverhangSettings.h"

/** Pure numerical kernel. The caller owns noise sampling and the choice of base density. */
namespace VoxelLandM4Overhang
{
/** Rejects an invalid configuration, including one which is disabled. */
VOXELLANDALGO_API bool IsValid(const FVoxelLandM4OverhangSettings& Settings);

/** Clamp finite noise to [-1, 1], then round to [-32767, 32767], ties away from zero.
     * On failure OutNoise is unchanged. Noise sampling itself is not implemented here.
     */
VOXELLANDALGO_API bool QuantizeNoise(float Noise, int32& OutNoise);

/**
     * Compute a Q8 displacement at HeightDifferenceQ8 = Z - H(X,Y).
     * NoiseQ must be in [-32767, 32767]. All inputs are validated even when disabled;
     * a valid disabled or zero-amplitude configuration returns exactly zero.
     * OutDeltaQ8 is unchanged on failure. One rational rounding is performed, ties away
     * from zero. The caller adds this displacement to its selected base density.
     */
VOXELLANDALGO_API bool DisplacementQ8(
	const FVoxelLandM4OverhangSettings& Settings, int64 HeightDifferenceQ8, int32 NoiseQ, int32& OutDeltaQ8);
}
