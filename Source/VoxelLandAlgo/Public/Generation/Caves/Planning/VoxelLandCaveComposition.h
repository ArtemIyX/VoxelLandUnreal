#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

struct FVoxelLandM5CavePlan;
struct FVoxelLandM5PointCm;

// Pure sample composition over the already quantized and filtered M4 field.
// This helper does not validate the plan, derive roof/protection masks, or
// certify the final mesh. The caller must validate feature ID uniqueness and
// geometry once per plan and must supply a conservatively derived carve mask.
namespace VoxelLand::M5::Composition
{
// Cave distances are Q8 centimetres, negative inside cave air. PocketDistanceQ8
// is an already converted signed distance contribution, not raw noise. If
// enabled and permitted, hard subtraction is max(M4Density, -C) after rounding
// -C * 4 / VoxelSizeCm to nearest, ties away from zero, and saturating to int16.
// All primitives are scanned, including their influence beyond physical radius.
// Protected samples always retain M4Density. On failure or cancellation Out is
// unchanged. Disabled, protected, and disallowed samples bypass plan evaluation.
VOXELLANDALGO_API bool TryCompose(int16 M4Density, const FVoxelLandM5CavePlan& Plan,
	const FVoxelLandM5PointCm& PositionCm, int32 VoxelSizeCm, bool bEnabled, bool bCarveAllowed, bool bProtected,
	bool bHasPocket, int64 PocketDistanceQ8, TFunctionRef<bool()> IsCancelled, int16& Out);
}
