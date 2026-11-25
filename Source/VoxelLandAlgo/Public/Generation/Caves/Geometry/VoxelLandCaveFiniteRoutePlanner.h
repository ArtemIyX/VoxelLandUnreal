#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Data/Caves/Geometry/VoxelLandCavePointCm.h"
#include "Templates/Function.h"

struct FVoxelLandM5CaveSettings;
struct FVoxelLandM5RegionCoord;
class FVoxelLandM5Budget;
class FVoxelLandM5CandidateResult;

namespace VoxelLand::M5::FiniteRoutePlanner
{
// The synchronous resolver proposes feet for a centerline point, retains the
// same XY, and must charge its own work and resources to Budget. Its result is
// intent, not evidence of support or final-field clearance. The caller retains
// the resolver and all callback resources until Build returns.
//
// Builds one scratch route per finite connection, including both entrance
// anchors, from complete unbranched segment chains. Settings and candidate must
// remain immutable throughout the call. No result is a certificate.
// Invalid topology, unresolved feet, budget failure or cancellation preserves Out.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Build(const FVoxelLandM5CaveSettings& Settings, int64 WorldSeed,
	const FVoxelLandM5RegionCoord& Region, TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<FVoxelLandM5Diagnostic(const FVoxelLandM5PointCm&, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe>,
		TFunctionRef<bool()>, FVoxelLandM5PointCm&)>
		ResolveFeet,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe>& Out);
}
