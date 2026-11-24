#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5CapsuleProfile.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Data/Structs/VoxelLandM5RegionCoord.h"
#include "Data/Structs/VoxelLandM5RouteLeg.h"
#include "Data/Structs/VoxelLandM5Segment.h"
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
