#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Generation/Caves/Geometry/VoxelLandCaveMultiRouteSupportSelection.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

struct FVoxelLandM5CaveSettings;
struct FVoxelLandM5StageManifest;
class FVoxelLandM5BaselineWindow;
class FVoxelLandM5Budget;
class FVoxelLandM5CandidateResult;
class FVoxelLandM5ProtectionGrid;
class FVoxelLandM5ScratchChunks;
struct FVoxelLandM5RetainedRouteInputs;

namespace VoxelLand::M5::ScratchRouteChecks
{
// Checks exact retained mesh and composition lineage; no publication authority.
VOXELLANDALGO_API FVoxelLandM5Diagnostic CheckAll(const FVoxelLandM5RetainedRouteInputs& Inputs,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);

// Synchronous finite CPU route checks over the supplied scratch chunks.
// Exact immediate source owners, seed, manifest and candidate settings must
// match those used for composition; every retained input must share Budget.
// Selections name faces in the freshly generated CPU meshes. Positive margins
// require the bounded coplanar support allowance and inflated obstacle checks.
//
// Success is not a world identity, source freshness, full influence, water,
// roof, renderer-transform, opening or publication certificate. Callers must
// establish those obligations and retain immutable inputs throughout the call.
// Distinct routed candidates must be direct RouteBuilder descendants of the
// exact composition source retained by Chunks. Identical handles are allowed.
VOXELLANDALGO_API FVoxelLandM5Diagnostic CheckAll(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5StageManifest& Manifest, TSharedRef<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Window,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> CompositionSource,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> RoutedCandidate,
	TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> Protection,
	TSharedRef<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Chunks, int64 ContactSkinCm,
	TConstArrayView<FVoxelLandM5MultiRouteSupportSelection> Selections,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);

VOXELLANDALGO_API FVoxelLandM5Diagnostic CheckAll(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5StageManifest& Manifest, TSharedRef<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Window,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> Protection,
	TSharedRef<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Chunks, int64 ContactSkinCm,
	TConstArrayView<FVoxelLandM5MultiRouteSupportSelection> Selections,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
}
