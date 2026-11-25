#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Planning/VoxelLandCaveSettings.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Data/Caves/Planning/VoxelLandCaveRegionCoord.h"
#include "Generation/Caves/Geometry/VoxelLandCaveMultiRouteSupportSelection.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

class FVoxelLandM5Budget;
class FVoxelLandM5CandidateResult;
class FVoxelLandM5ScratchMesh;
struct FVoxelLandChunkData;

namespace VoxelLand::M5::MultiRouteChecks
{
// Verifies ideal route/feature coverage and checks every leg against the
// production CPU meshes of the complete supplied rectangular chunk set.
// Exactly one support-face selection must identify each (route ID, leg index).
// All input storage remains immutable and admitted by the caller.
//
// This bounded CPU geometry check does not prove field/descriptor provenance,
// renderer transforms, roof/protection, complete plan or region influence, or
// portal publication. It is not a final plan or player-movement certificate.
// Unsupported mesh ownership, contact or geometry returns Unproven.
VOXELLANDALGO_API FVoxelLandM5Diagnostic CheckAll(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& Region, TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TConstArrayView<FVoxelLandChunkData> Chunks, const FIntVector& MinInclusive, const FIntVector& MaxExclusive,
	int32 VoxelSizeCm, bool bUnlimitedM4, int64 ContactSkinCm,
	TConstArrayView<FVoxelLandM5MultiRouteSupportSelection> Selections,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);

VOXELLANDALGO_API FVoxelLandM5Diagnostic CheckAll(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& Region, TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	const FVoxelLandM5ScratchMesh& Mesh, int64 ContactSkinCm,
	TConstArrayView<FVoxelLandM5MultiRouteSupportSelection> Selections,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
}
