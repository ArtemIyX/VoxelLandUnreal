#pragma once

#include "CoreMinimal.h"
#include "Generation/Caves/Geometry/VoxelLandCaveMultiRouteSupportSelection.h"

struct FVoxelLandM5CaveSettings;
struct FVoxelLandM5StageManifest;
class FVoxelLandM5BaselineWindow;
class FVoxelLandM5CandidateResult;
class FVoxelLandM5ProtectionGrid;
class FVoxelLandM5ScratchMesh;

// Synchronous borrowed inputs; all owners and selection storage outlive the call.
struct VOXELLANDALGO_API FVoxelLandM5RetainedRouteInputs
{
	const FVoxelLandM5CaveSettings& Settings;
	int64 CaveSeed;
	const FVoxelLandM5StageManifest& Manifest;
	const FVoxelLandM5BaselineWindow& Baseline;
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> CompositionSource;
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Routed;
	TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> Protection;
	const FVoxelLandM5ScratchMesh& Mesh;
	int64 ContactSkinCm;
	TConstArrayView<FVoxelLandM5MultiRouteSupportSelection> Selections;
};
