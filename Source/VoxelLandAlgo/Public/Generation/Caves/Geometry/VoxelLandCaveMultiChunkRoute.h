#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Data/Caves/Geometry/VoxelLandCavePointCm.h"
#include "Generation/Caves/Planning/VoxelLandCaveChunkFaceSelection.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;
class FVoxelLandM5ScratchMesh;
struct FVoxelLandChunkData;

namespace VoxelLand::M5::MultiChunkRoute
{
// Certifies a single linear capsule interval against the production CPU meshes
// of every supplied LOD-zero chunk in the complete half-open chunk rectangle.
// All shared density halos must agree, and the inflated sweep must lie strictly
// inside the rectangle by two voxels, so omitted neighboring mesh owners cannot
// reach it. Input chunks and face selections remain caller-owned and immutable.
// A positive margin permits coplanar support contact only when the bounded
// proof confines all overlap to the lower cap within the nominal footprint.
// All other faces undergo the inflated obstacle sweep; uncertain bounds fail.
//
// Success establishes CPU support and obstacle clearance for this interval only.
// It does not prove field/descriptor provenance, renderer transforms, route or
// feature coverage, roof/protection, or region publication. It cannot authorize
// a plan. Unsupported geometry, incomplete chunks or uncertain predicates fail.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Certify(TConstArrayView<FVoxelLandChunkData> Chunks,
	const FIntVector& MinInclusive, const FIntVector& MaxExclusive, int32 VoxelSizeCm, bool bUnlimitedM4,
	const FVoxelLandM5PointCm& StartFeet, const FVoxelLandM5PointCm& EndFeet, int64 RadiusCm, int64 HeightCm,
	int64 MarginCm, int64 ContactSkinCm, int32 MaxSlopeNumerator, int32 MaxSlopeDenominator,
	TConstArrayView<FVoxelLandM5ChunkFaceSelection> SupportFaces,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);

// Uses the retained, exact CPU mesh and its complete source rectangle.
VOXELLANDALGO_API FVoxelLandM5Diagnostic Certify(const FVoxelLandM5ScratchMesh& Mesh,
	const FVoxelLandM5PointCm& StartFeet, const FVoxelLandM5PointCm& EndFeet, int64 RadiusCm, int64 HeightCm,
	int64 MarginCm, int64 ContactSkinCm, int32 MaxSlopeNumerator, int32 MaxSlopeDenominator,
	TConstArrayView<FVoxelLandM5ChunkFaceSelection> SupportFaces,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
}
