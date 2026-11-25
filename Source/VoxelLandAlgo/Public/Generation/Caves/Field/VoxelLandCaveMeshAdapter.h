#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Generation/Caves/Geometry/VoxelLandCaveValidationTriangle.h"
#include "Templates/Function.h"

struct FVoxelLandChunkData;
struct FVoxelLandMeshData;

namespace VoxelLand::M5::MeshAdapter
{
// Adapt a CPU Surface Nets mesh from this exact chunk, generated with this
// voxel size and meshing mode. The caller must establish mesh provenance and
// provide every owning neighbor required by the validation volume; a count
// check alone cannot establish either. Render transforms and rebasing are not
// covered. No success here establishes a closed mesh or a walkable path.
//
// Every sign-changing owned edge must have both expected triangles. Surface
// Nets may discard degenerate faces; missing faces return Unproven. Bad indices,
// malformed arrays, out-of-envelope values, cancellation and exhaustion are
// rejected without modifying OutTriangles. Each returned integer box includes
// an outward-rounded enclosure of the exact CPU local-plus-origin sum; A/B/C
// are the corresponding nearest binary64 values in index/winding order.
// Every stored sample and its one-cell halo must fit the logical M5 envelope,
// even when no triangles are emitted. Nondegeneracy is established with padded
// binary64 intervals; ambiguous or subnormal input coordinates are rejected.
// Requires normal IEEE binary32/binary64 operations without unsafe fast-math.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Adapt(const FVoxelLandChunkData& Chunk, float VoxelSizeCm, bool bUnlimitedM4,
	const FVoxelLandMeshData& Mesh, int32 MaxTriangles, int64 MaxWork, TFunctionRef<bool()> IsCancelled,
	TArray<FVoxelLandM5ValidationTriangle>& OutTriangles);
}
