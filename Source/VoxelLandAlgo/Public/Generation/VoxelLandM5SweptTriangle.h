#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Data/Structs/VoxelLandM5PointCm.h"
#include "Generation/VoxelLandM5ValidationTriangle.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;

namespace VoxelLand::M5::SweptTriangle
{
// A sufficient continuous separation certificate, not a walkability proof.
// The swept capsule uses linearly translated upright feet positions; HeightCm
// includes both hemispheres. MarginCm expands its radius about the unchanged
// medial segment. TriangleErrorCm bounds the absolute per-axis difference
// between each supplied binary64 vertex and the actual final-mesh vertex.
//
// Every supplied triangle is an obstacle: none is exempted as floor. None
// certifies separation only from these triangles. Callers must establish mesh
// provenance, completeness, transform/error bounds, floor contact/support,
// transitions, roof and protection before publishing a plan. A separating axis
// missed by this bounded algorithm yields Unproven rather than a collision.
// Requires precise IEEE binary64, nearest rounding and gradual underflow; any
// unsupported arithmetic environment fails closed. All positions and capsule
// medial endpoints must fit the M5 +/-1,048,576 cm envelope. Work is charged
// cumulatively to Budget; input arrays and their leases remain caller-owned.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Check(const FVoxelLandM5PointCm& StartFeet, const FVoxelLandM5PointCm& EndFeet,
	int64 RadiusCm, int64 HeightCm, int64 MarginCm, const FVector3d& TriangleErrorCm,
	TConstArrayView<FVoxelLandM5ValidationTriangle> Triangles,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
}
