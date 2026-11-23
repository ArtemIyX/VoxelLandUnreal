#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"

struct FVoxelLandM5CaveSettings;
struct FVoxelLandM5StageManifest;
class FVoxelLandM5BaselineWindow;
class FVoxelLandM5Budget;
class FVoxelLandM5CandidateResult;
class FVoxelLandM5FinalField;
class FVoxelLandM5ProtectionGrid;

namespace VoxelLand::M5::WindowFinalField
{
// Finite scratch composition only; inputs must remain immutable through Build.
// A network requires a candidate; a pocket-only stage requires a null candidate.
// The caller must prove baseline and mask provenance, complete surface and
// influence domains, water, routes and final-field safety before publication.
// Failure or cancellation leaves OutField unchanged.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Build(const FVoxelLandM5CaveSettings& Settings,
	const FVoxelLandM5StageManifest& Manifest, int64 WorldSeed, const FVoxelLandM5BaselineWindow& Window,
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	const FVoxelLandM5ProtectionGrid* Protection, FIntVector Start, FIntVector Count,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5FinalField, ESPMode::ThreadSafe>& OutField);
// Compatibility overload for network candidates.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Build(const FVoxelLandM5CaveSettings& Settings,
	const FVoxelLandM5StageManifest& Manifest, int64 WorldSeed, const FVoxelLandM5BaselineWindow& Window,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	const FVoxelLandM5ProtectionGrid* Protection, FIntVector Start, FIntVector Count,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5FinalField, ESPMode::ThreadSafe>& OutField);
}
