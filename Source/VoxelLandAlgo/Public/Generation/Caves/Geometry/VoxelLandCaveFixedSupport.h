#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Generation/Caves/Geometry/VoxelLandCaveFixedPointQ18.h"
#include "Generation/Caves/Geometry/VoxelLandCaveFixedTriangleQ18.h"
#include "Templates/Function.h"

class FVoxelLandM5Budget;

namespace VoxelLand::M5::FixedSupport
{
// Certify contact against a single coplanar patch of exact Q18 vertices. All
// coordinates, capsule dimensions and contact skin are in Q18 centimetres.
// The caller must establish complete mesh provenance and separately check
// obstacles, route transitions and every other condition of walkability.
//
// This adapter never rounds input geometry: it subtracts one common origin
// and divides every coordinate and dimension by the same exact power of two,
// then applies the existing integer contact/coverage proof in those units.
// It returns Unproven if that lossless representation cannot fit its bounded
// arithmetic, if the exact plane cannot be represented, or if any renderer
// uncertainty is nonzero. Zero renderer uncertainty is not implied by CPU
// mesh coordinates. Input storage and its admission remain caller-owned.
// Additionally certifies that all support-plane points within the inflated
// horizontal reach remain strictly below the lower medial center throughout
// the sweep, and any possible overlap lies strictly within the nominal
// support footprint. Only that lower-cap contact is permitted; all other
// triangles must still pass the inflated obstacle sweep.
VOXELLANDALGO_API FVoxelLandM5Diagnostic CertifyLinearContactWithMargin(const FVoxelLandM5FixedPointQ18& FeetStart,
	const FVoxelLandM5FixedPointQ18& FeetEnd, int64 RadiusQ18, int64 HeightQ18, int64 ContactSkinQ18, int64 MarginQ18,
	int32 MaxSlopeNumerator, int32 MaxSlopeDenominator, TConstArrayView<FVoxelLandM5FixedTriangleQ18> Triangles,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);

VOXELLANDALGO_API FVoxelLandM5Diagnostic CertifyLinearContact(const FVoxelLandM5FixedPointQ18& FeetStart,
	const FVoxelLandM5FixedPointQ18& FeetEnd, int64 RadiusQ18, int64 HeightQ18, int64 ContactSkinQ18,
	int32 MaxSlopeNumerator, int32 MaxSlopeDenominator, TConstArrayView<FVoxelLandM5FixedTriangleQ18> Triangles,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled);
}
