#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Data/Structs/VoxelLandM5PointCm.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;
struct FVoxelLandChunkData;

namespace VoxelLand::M5::SingleChunkRoute
{
// Certifies one linear upright-capsule interval against a CPU Surface Nets
// mesh generated inside this call from Chunk. SupportIndices identify faces
// in that freshly generated mesh; every other face is an obstacle. The support
// proof independently requires all selected faces to form one exact coplanar
// patch and checks nominal capsule nonpenetration, contact and slope.
// Nonzero MarginCm returns Unproven until support-face clearance is certified.
//
// The entire inflated sweep must be strictly inside the chunk by two voxels,
// so an edge owned by a neighboring chunk cannot contribute an intersecting
// triangle. Ambiguous/degenerate meshes, unsupported precision or contact and
// uncertain predicates fail closed. This proves CPU geometry for this single
// interval only. It does not establish that Chunk is the final world field,
// that all planned routes or entrances are covered, roof/protection/water,
// region agreement, renderer transforms or gameplay movement. It is not a
// plan-publication certificate. Chunk storage remains caller-owned/immutable.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Certify(const FVoxelLandChunkData& Chunk, int32 VoxelSizeCm, bool bUnlimitedM4,
	const FVoxelLandM5PointCm& StartFeet, const FVoxelLandM5PointCm& EndFeet, int64 RadiusCm, int64 HeightCm,
	int64 MarginCm, int64 ContactSkinCm, int32 MaxSlopeNumerator, int32 MaxSlopeDenominator,
	TConstArrayView<int32> SupportIndices, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled);
}
