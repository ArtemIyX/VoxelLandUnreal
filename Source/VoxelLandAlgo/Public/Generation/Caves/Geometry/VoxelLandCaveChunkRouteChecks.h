#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Planning/VoxelLandCaveSettings.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Data/Caves/Planning/VoxelLandCaveRegionCoord.h"
#include "Generation/Caves/Geometry/VoxelLandCaveRouteSupportSelection.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

class FVoxelLandM5Budget;
class FVoxelLandM5CandidateResult;
struct FVoxelLandChunkData;

namespace VoxelLand::M5::ChunkRouteChecks
{
// Runs ideal-route validation and certifies every route leg exactly once
// against a CPU Surface Nets mesh constructed from this one supplied chunk.
// A separate support selection is required for each (route ID, leg index).
// All input storage must remain immutable and be admitted by the caller.
//
// This restricted check does NOT establish field or mesh provenance, renderer
// transforms, roof/protection/water, region closure, entrance openings, or
// that this chunk covers the full plan. It is not a plan-publication or player
// movement certificate. Neighbor-owned or unsupported geometry is Unproven.
// ContactSkinCm is a geometric tolerance, not a movement-step allowance.
VOXELLANDALGO_API FVoxelLandM5Diagnostic CheckAll(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& Region, TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	const FVoxelLandChunkData& Chunk, int32 VoxelSizeCm, bool bUnlimitedM4, int64 ContactSkinCm,
	TConstArrayView<FVoxelLandM5RouteSupportSelection> Selections,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
}
