#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5CaveSettings.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Data/Structs/VoxelLandM5RegionCoord.h"
#include "Generation/VoxelLandM5PortalOverlap.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

class FVoxelLandM5Budget;
class FVoxelLandM5CandidateResult;

namespace VoxelLand::M5::PortalHandshake
{
// Checks exactly one supplied face-adjacent pair. Descriptor bytes and overlap
// samples must be retained immutable by their caller and be from the actual
// corresponding validated final fields. This routine compares those inputs;
// it does not establish their provenance, sufficient density influence, final
// field safety, complete region-set closure, or permission to publish.
// VoxelSizeCm and HaloSamples are validated descriptor and mesher inputs.
// Blocks must use the M4 global sample lattice anchored at logical zero and
// cover the portal plus the explicit sample halo in every direction.
// Topology validation and work are charged to the same supplied budget.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Check(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& RegionA, const FVoxelLandM5RegionCoord& RegionB,
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> A,
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> B, TConstArrayView<uint8> ExpectedDescriptor,
	TConstArrayView<uint8> DescriptorA, TConstArrayView<uint8> DescriptorB, int32 VoxelSizeCm, int32 HaloSamples,
	const FVoxelLandM5PortalOverlap& SamplesA, const FVoxelLandM5PortalOverlap& SamplesB,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
}
