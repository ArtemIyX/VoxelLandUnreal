#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Generation/VoxelLandM5FixedPointQ18.h"
#include "Generation/VoxelLandM5FixedTriangleQ18.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;

namespace VoxelLand::M5::PiecewiseSupport
{
// Scratch certificate only. Inputs require exact renderer coordinates. A fast
// lossless local proof falls back to checked 512-bit Q18 predicates. At most
// 256 faces and depth 8 are supported;
// overflow or unresolved intervals return Unproven. Both variants prove support
// contact and nominal nonpenetration. The nominal variant does not establish a
// lower-cap exemption. WithMargin additionally bounds every selected plane's
// inflated overlap to its nominal lower-cap footprint. Neither establishes mesh
// completeness, obstacle clearance, route transitions, or walkability.
VOXELLANDALGO_API FVoxelLandM5Diagnostic CertifyLinearContactWithMargin(const FVoxelLandM5FixedPointQ18& Start,
	const FVoxelLandM5FixedPointQ18& End, int64 RadiusQ18, int64 HeightQ18, int64 SkinQ18, int64 MarginQ18,
	int32 MaxSlopeNumerator, int32 MaxSlopeDenominator, TConstArrayView<FVoxelLandM5FixedTriangleQ18> Triangles,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
VOXELLANDALGO_API FVoxelLandM5Diagnostic CertifyLinearContact(const FVoxelLandM5FixedPointQ18& Start,
	const FVoxelLandM5FixedPointQ18& End, int64 RadiusQ18, int64 HeightQ18, int64 SkinQ18, int32 MaxSlopeNumerator,
	int32 MaxSlopeDenominator, TConstArrayView<FVoxelLandM5FixedTriangleQ18> Triangles,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
}
