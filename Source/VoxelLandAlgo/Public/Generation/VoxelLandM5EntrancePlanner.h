#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5CavePlan.h"
#include "Data/Structs/VoxelLandM5CaveSettings.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

class FVoxelLandM5Budget;
class FVoxelLandM5CandidateResult;

namespace VoxelLand::M5::EntrancePlanner
{
// Query must use immutable actual final-surface data, not a height-grid proxy.
// For the requested horizontal radius it must certify one usable surface crossing
// across the complete opening footprint. Return Unproven for missing or ambiguous
// data; bProtected describes the entire footprint, not just its center.
// The radius supplied to the query covers the ramp's geometric footprint.
// MinSurfaceZ and MaxSurfaceZ must conservatively bound every actual crossing
// in that footprint; this initial planner rejects any varying height range.
// Hard subtraction can alter densities beyond this footprint: the separate
// final-field validator must cover the complete density influence.
// Both candidate centerline segments obey a conservative integer slope bound;
// that does not certify their floor, collision clearance, or the final mesh.
// Output is an unvalidated candidate: its path slope, final mesh, floor support,
// capsule clearance, roof/protection and neighboring regions require validation.
// The result and all of its arrays remain unchanged on failure or cancellation.
// QuerySurface is an explicit trust boundary: it must charge all of its own
// allocations and work to the supplied budget. An arbitrary callback cannot be
// forced to comply, so the publication barrier must use a trusted metered provider.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Build(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<FVoxelLandM5Diagnostic(TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, int64 X, int64 Y,
		int32 RadiusCm, int64& SurfaceZ, int64& MinSurfaceZ, int64& MaxSurfaceZ, int32& ConservativeSlopeDegrees,
		bool& bProtected, bool& bFlooded)>
		QuerySurface,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>& Out);
}
