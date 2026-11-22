#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5CapsuleProfile.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Data/Structs/VoxelLandM5Segment.h"
#include "Generation/VoxelLandM5FeetProposal.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;

namespace VoxelLand::M5::FeetProposalBuilder
{
// Produces a conservative proposal for one constant-radius ideal segment. At
// each interpolation parameter the inflated upright capsule lies in the
// segment's ideal swept-ball volume. Its centerline slope is conservatively
// bounded by MaxSlopeDegrees. No floor contact or final-mesh safety follows.
// Rejects unsupported coordinates and geometry; Out is unchanged on failure.
// Charges 32 work units and uses no heap scratch. Caller retains input storage.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Build(const FVoxelLandM5Segment& Segment,
	const FVoxelLandM5CapsuleProfile& Capsule, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, FVoxelLandM5FeetProposal& Out);
}
