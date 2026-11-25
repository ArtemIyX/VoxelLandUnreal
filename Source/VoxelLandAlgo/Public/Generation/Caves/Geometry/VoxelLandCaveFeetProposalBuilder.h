#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Geometry/VoxelLandCaveCapsuleProfile.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Data/Caves/Geometry/VoxelLandCaveSegment.h"
#include "Generation/Caves/Geometry/VoxelLandCaveFeetProposal.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;

namespace VoxelLand::M5::FeetProposalBuilder
{
// Produces a conservative proposal for one constant-radius ideal segment. At
// each interpolation parameter the inflated upright capsule lies in the
// segment's ideal swept-ball volume. Its centerline slope is conservatively
// bounded by MaxSlopeDegrees. No floor contact or final-mesh safety follows.
// Rejects unsupported coordinates and geometry; Out is unchanged on failure.
// Version 1 charges 32 units; version 2 charges 64. No heap scratch is used.
// Version 2 proves ideal profile containment only and does not establish contact.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Build(const FVoxelLandM5Segment& Segment,
	const FVoxelLandM5CapsuleProfile& Capsule, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, FVoxelLandM5FeetProposal& Out);
}
