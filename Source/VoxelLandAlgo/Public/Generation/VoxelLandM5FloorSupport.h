#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Generation/VoxelLandM5ClosedBoxCm.h"
#include "Generation/VoxelLandM5FloorTriangleCm.h"
#include "Templates/Function.h"

namespace VoxelLand::M5::FloorSupport
{
// A deliberately restricted sufficient proof: the entire inclusive horizontal
// footprint rectangle must be covered by a union of complete axis-aligned
// rectangular tiles, each represented by exactly two CCW horizontal triangles.
// Other triangulations, fractional coordinates and slopes are Unproven. The
// caller must conservatively enclose the full moving contact footprint, supply
// triangles from the actual final mesh without conversion uncertainty, and
// separately establish mesh completeness, capsule nonpenetration, ceilings,
// route transitions and movement behavior. Success is NOT a walkability proof.
// Footprint Min.Z and Max.Z must be the same floor height. MaxChecks caps pair
// and rectangle inspections; at most 256 triangles are accepted per call.
VOXELLANDALGO_API FVoxelLandM5Diagnostic CheckHorizontalRectangularSupport(const FVoxelLandM5ClosedBoxCm& Footprint,
	TConstArrayView<FVoxelLandM5FloorTriangleCm> Triangles, int64 MaxChecks, TFunctionRef<bool()> IsCancelled);
}
