#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Data/Structs/VoxelLandM5PointCm.h"
#include "Generation/VoxelLandM5FixedTriangleQ18.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;

namespace VoxelLand::M5::MeshFeetProposal
{
// Proposes feet at Center.X/Y, on the highest upward-facing triangle strictly
// inside its XY projection and within [LowerZCm, Center.Z]. Exact horizontal
// intersections use their exact height; other intersections require directed
// bounds to agree on ceil(height in cm). Boundary hits, overlapping height
// intervals and unsupported arithmetic fail closed. Inputs are caller-owned,
// immutable exact Q18 triangles from FixedMesh::Convert with verified provenance;
// nonzero renderer uncertainty is rejected. Complete mesh ownership, renderer
// transforms, provider resources and storage are caller obligations.
// Requires IEEE binary64 without unsafe fast-math. A proposal is not support,
// clearance, route safety or a certificate. Failure
// or cancellation leaves OutFeet unchanged. Worker thread not required.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Build(const FVoxelLandM5PointCm& Center, int64 LowerZCm,
	TConstArrayView<FVoxelLandM5FixedTriangleQ18> Triangles, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, FVoxelLandM5PointCm& OutFeet);
}
