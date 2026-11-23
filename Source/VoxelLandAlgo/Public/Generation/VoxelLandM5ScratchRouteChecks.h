#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Generation/VoxelLandM5MultiRouteSupportSelection.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

struct FVoxelLandM5CaveSettings;
struct FVoxelLandM5StageManifest;
class FVoxelLandM5BaselineWindow;
class FVoxelLandM5Budget;
class FVoxelLandM5CandidateResult;
class FVoxelLandM5ProtectionGrid;
class FVoxelLandM5ScratchChunks;

namespace VoxelLand::M5::ScratchRouteChecks
{
// Synchronous finite CPU route checks over the supplied scratch chunks.
// Exact immediate source owners, seed, manifest and candidate settings must
// match those used for composition; every retained input must share Budget.
// Selections name faces in the freshly generated CPU meshes. Positive
// clearance margins remain Unproven by the underlying support-face check.
//
// Success is not a world identity, source freshness, full influence, water,
// roof, renderer-transform, opening or publication certificate. Callers must
// establish those obligations and retain immutable inputs throughout the call.
VOXELLANDALGO_API FVoxelLandM5Diagnostic CheckAll(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5StageManifest& Manifest,
	TSharedRef<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Window,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> Protection,
	TSharedRef<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Chunks, int64 ContactSkinCm,
	TConstArrayView<FVoxelLandM5MultiRouteSupportSelection> Selections,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
}
