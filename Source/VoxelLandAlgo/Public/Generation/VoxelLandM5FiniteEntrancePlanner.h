#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

struct FVoxelLandM5CaveSettings;
class FVoxelLandM5BaselineWindow;
class FVoxelLandM5Budget;
class FVoxelLandM5CandidateResult;
class FVoxelLandM5ProtectionGrid;

namespace VoxelLand::M5::FiniteEntrancePlanner
{
// Synchronous finite scratch planning against the actual baseline window using
// the flat-midpoint query. The dry policy is an owner declaration, not a water
// certificate. A required protection grid must be supplied, and the candidate,
// window and any grid must use Budget. Inputs and cancellation predicate remain
// immutable and alive for the duration of the call.
//
// The caller must prove complete world-height and baseline identity provenance,
// mesher/filter influence and source freshness. This does not certify the final
// field, dry cavities, route clearance, roof, opening safety or publication.
// Unsupported surfaces, missing halo, budget failure and cancellation fail
// closed. Out is unchanged on every failure.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Build(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	const FVoxelLandM5BaselineWindow& Window, const FVoxelLandM5ProtectionGrid* Protection,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, int64 MaxSamplesPerQuery,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>& Out);
}
