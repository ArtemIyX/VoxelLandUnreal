#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Generation/VoxelLandM5ClosedBoxCm.h"
#include "Generation/VoxelLandM5FloorTriangleCm.h"
#include "Generation/VoxelLandM5SupportPlane.h"
#include "Templates/Function.h"

namespace VoxelLand::M5::SlopedFloorSupport
{
// Sufficient proof of a complete XY rectangle covered by consistently wound,
// paired rectangular triangle tiles on ONE exact plane. Coordinates must be
// integral centimetres in [-1048576,1048576]. Plane coefficients and the
// nonnegative slope bound numerator and positive denominator are <= 32767.
// The slope bound is tan(max angle), supplied as an exact rational fraction.
// Only XY of Footprint is used; its Z must be zero. Unknown triangulation,
// noncoplanarity or a slope above the bound returns Unproven. Success proves
// coverage/slope only: the caller must prove actual final-mesh provenance,
// capsule contact/nonpenetration, full continuous clearance, route transitions,
// steps, and complete neighboring geometry. This is not a walkability proof.
VOXELLANDALGO_API FVoxelLandM5Diagnostic CheckCoplanarRectangularSupport(const FVoxelLandM5ClosedBoxCm& Footprint,
	const FVoxelLandM5SupportPlane& Plane, int32 MaxSlopeNumerator, int32 MaxSlopeDenominator,
	TConstArrayView<FVoxelLandM5FloorTriangleCm> Triangles, int64 MaxChecks, TFunctionRef<bool()> IsCancelled);
}
