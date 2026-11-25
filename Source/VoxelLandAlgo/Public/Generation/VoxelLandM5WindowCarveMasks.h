#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"

struct FVoxelLandM5CaveSettings;
class FVoxelLandM5BaselineWindow;
class FVoxelLandM5Budget;
class FVoxelLandM5CandidateResult;
class FVoxelLandM5CarveMasks;
class FVoxelLandM5ProtectionGrid;
class FVoxelLandM5BoundBaseline;
class FVoxelLandM5InfluenceSet;
class FVoxelLandM5PocketSurfaceColumns;

namespace VoxelLand::M5::WindowCarveMasks
{
// Finite scratch assembly only. Inputs must remain immutable for the call.
// A network requires a candidate; a pocket-only stage requires a null candidate.
// Each selected sample's entrance exception is obtained from EntranceMask,
// which validates candidate settings/seed and the closed +/- two-voxel box.
// Protection overrides exceptions. The dry-world input is an owner declaration,
// not proof of water-free final geometry. The caller must establish baseline and
// protection provenance, complete vertical/surface domains, actual filter and
// mesher influence, and all final-field safety and route obligations. This is
// neither a certificate nor permission to publish. Failure preserves OutMasks.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Build(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5BaselineWindow& Window,
	TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	const FVoxelLandM5ProtectionGrid* Protection, FIntVector Start, FIntVector Count,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe>& OutMasks);
// Requires exact bound owners and complete streamed columns. Failure preserves output.
VOXELLANDALGO_API FVoxelLandM5Diagnostic BuildOwned(const FVoxelLandM5BoundBaseline& Baseline,
	const FVoxelLandM5InfluenceSet& Influence, const FVoxelLandM5PocketSurfaceColumns& Columns,
	const FVoxelLandM5BaselineWindow& Window, TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	FIntVector Start, FIntVector Count, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe>& OutMasks);
// Compatibility overload for network candidates.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Build(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5BaselineWindow& Window,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	const FVoxelLandM5ProtectionGrid* Protection, FIntVector Start, FIntVector Count,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe>& OutMasks);
}
