#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5CavePlan.h"
#include "Data/Structs/VoxelLandM5CaveSettings.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Templates/Function.h"

namespace VoxelLand::M5::EntrancePlanner
{
// Query must use immutable actual final-surface data, not a height-grid proxy.
// For the requested horizontal radius it must certify one usable surface crossing
// across the complete opening footprint. Return false for missing or ambiguous
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
VOXELLANDALGO_API FVoxelLandM5Diagnostic Build(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5CavePlan& Candidate,
	TFunctionRef<bool(int64 X, int64 Y, int32 RadiusCm, int64& SurfaceZ, int64& MinSurfaceZ, int64& MaxSurfaceZ,
		int32& ConservativeSlopeDegrees, bool& bProtected, bool& bFlooded)>
		QuerySurface,
	TFunctionRef<bool()> IsCancelled, FVoxelLandM5CavePlan& Out);
}
