#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Generation/VoxelLandM5FloorTriangleCm.h"
#include "Generation/VoxelLandM5SupportPlane.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;

namespace VoxelLand::M5::TriangleSupport
{
// Certifies a single linear upright-capsule interval against one exact,
// integer-centimetre plane and an upward-wound coplanar triangle patch.
// Requires a complete XY rectangle enclosing the lower-sphere projected sweep
// to be covered by the patch, and verifies nominal nonpenetration, contact
// skin and slope. Uncertain arrangements are rejected rather than sampled.
//
// This is only a sufficient contact certificate. The caller must prove the
// input triangles are the complete actual final mesh, separately certify all
// obstacle clearance (including non-support portions), region ownership and
// route transitions. It does not establish a complete walkable cave.
// Every coordinate (including the entire footprint and capsule) must lie in
// [-1048576,1048576] cm. All geometry is exact integer cm. Budget covers this
// routine's work; caller retains input storage and its admission lease.
VOXELLANDALGO_API FVoxelLandM5Diagnostic CertifyLinearContact(const FVoxelLandM5PointCm& FeetStart,
	const FVoxelLandM5PointCm& FeetEnd, int64 RadiusCm, int64 HeightCm, int64 ContactSkinCm,
	const FVoxelLandM5SupportPlane& Plane, int32 MaxSlopeNumerator, int32 MaxSlopeDenominator,
	TConstArrayView<FVoxelLandM5FloorTriangleCm> Triangles, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled);
}
