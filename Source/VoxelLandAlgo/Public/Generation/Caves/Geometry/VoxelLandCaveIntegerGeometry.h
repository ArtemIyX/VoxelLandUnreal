#pragma once

#include "CoreMinimal.h"

struct FVoxelLandM5PointCm;

// Integer-centimetre sphere and constant-radius segment primitives. Positions
// must be in [-1048576, 1048576] cm; radii must be in [1, 1048576] cm.
// Results are floor(distance * 256) - radius * 256 in Q8 centimetres.
// Invalid inputs leave Out unchanged. These distances alone do not certify
// capsule clearance or the safety of the quantized and meshed terrain.
namespace VoxelLand::M5::IntegerGeometry
{
VOXELLANDALGO_API bool TrySphereDistanceQ8(
	const FVoxelLandM5PointCm& P, const FVoxelLandM5PointCm& Center, int64 RadiusCm, int64& Out);
VOXELLANDALGO_API bool TrySegmentDistanceQ8(const FVoxelLandM5PointCm& P, const FVoxelLandM5PointCm& A,
	const FVoxelLandM5PointCm& B, int64 RadiusCm, int64& Out);
}
