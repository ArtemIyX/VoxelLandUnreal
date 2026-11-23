#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

struct FVoxelLandM5CaveSettings;
struct FVoxelLandM5StageManifest;
class FVoxelLandM5BaselineWindow;
class FVoxelLandM5Budget;
class FVoxelLandM5CandidateResult;
class FVoxelLandM5ProtectionGrid;
class FVoxelLandM5ScratchChunks;

namespace VoxelLand::M5::ScratchRoofCheck
{
// Checks baseline/final contributing samples around owned cells near any
// baseline mixed cell. Entire sample influence must fit one candidate opening
// before a roof cell is exempted. The baseline window must cover the full
// search and comparison halo; uniform-sign neighborhoods have no local roof.
// Caller must establish that this covers the complete relevant surface domain.
// Exact immediate inputs and budget must match. This does not prove surface provenance, opening authorization,
// mesher/filter influence, water, geometry safety, or publication readiness.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Check(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5StageManifest& Manifest,
	TSharedRef<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Baseline,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> Protection,
	TSharedRef<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Chunks,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
}
