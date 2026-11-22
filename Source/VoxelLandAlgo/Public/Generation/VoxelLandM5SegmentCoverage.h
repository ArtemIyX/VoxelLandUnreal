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
// A deliberately narrow sufficient check for ONE claimed segment: a Walk leg
// must name this region-qualified segment and traverse the complete canonical
// feet proposal in either direction. The proposal encloses the swept capsule
// inside the ideal segment volume and bounds its centerline slope.
//
// Success does not prove feature ownership/uniqueness, the remaining plan,
// anchors, floor contact, actual quantized mesh, roof or walkability. The caller
// must validate immutable source provenance, all route coverage and final field.
// The budget charges this check plus FeetProposalBuilder's work; input storage
// and its admission lease remain the caller's responsibility.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Check(const FVoxelLandM5Segment& Segment, const FVoxelLandM5RouteLeg& Leg,
	const FVoxelLandM5RegionCoord& Region, const FVoxelLandM5CapsuleProfile& Capsule,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
}
