#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Geometry/VoxelLandCaveCapsuleProfile.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Data/Caves/Planning/VoxelLandCaveRegionCoord.h"
#include "Data/Caves/Geometry/VoxelLandCaveRouteLeg.h"
#include "Data/Caves/Geometry/VoxelLandCaveSegment.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;

namespace VoxelLand::M5::SegmentCoverage
{
// Checks ONE region-qualified segment and a complete straight Walk leg in
// either direction. Canonical centered legs retain their inflated-ball proof.
// Explicit legs must match endpoint XY, have nonzero horizontal extent and a
// conservative feet slope; both nominal endpoint capsules must fit the segment
// endpoint balls. Convex interpolation contains the complete nominal sweep.
//
// Explicit legs do not prove inflated clearance, floor contact, feature
// ownership, anchors, final quantized mesh, roof or walkability. The caller must
// validate source provenance, complete route coverage and final geometry. The
// budget charges 40 work units; input storage and its lease remain caller-owned.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Check(const FVoxelLandM5Segment& Segment, const FVoxelLandM5RouteLeg& Leg,
	const FVoxelLandM5RegionCoord& Region, const FVoxelLandM5CapsuleProfile& Capsule,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
}
