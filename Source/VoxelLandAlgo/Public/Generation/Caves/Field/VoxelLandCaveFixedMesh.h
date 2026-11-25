#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Generation/Caves/Geometry/VoxelLandCaveFixedTriangleQ18.h"
#include "Generation/Caves/Geometry/VoxelLandCaveValidationTriangle.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;

namespace VoxelLand::M5::FixedMesh
{
// Convert one actual adapted Surface Nets triangle without rounding its CPU
// vertices. VoxelSizeCm must be an integer in [1,134217]. The caller must
// establish that Triangle was produced from the matching chunk and mesher;
// bVerifiedProvenance is an assertion of that external evidence, not a proof.
// RendererErrorCm must conservatively bound all later rendering transforms on
// each axis. A missing provenance assertion, unknown error, off-grid point or
// unsupported coordinate is rejected. Output is unchanged on all failures.
//
// Grid is 2^(frexp(Size*34).exponent-24), at least 2^-18 cm in this domain;
// Q18 therefore represents every point exactly. The +/- 2^20 cm envelope
// bounds coordinates by 2^38 Q18 units. Differences can reach 2^39; normal
// cross products can exceed 64 bits, and squared/dot predicates can require
// wider than 128 bits. No normal or walkability proof is implied here.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Convert(const FVoxelLandM5ValidationTriangle& Triangle, int32 VoxelSizeCm,
	bool bVerifiedProvenance, const FVector3d& RendererErrorCm,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	FVoxelLandM5FixedTriangleQ18& OutTriangle);
}
