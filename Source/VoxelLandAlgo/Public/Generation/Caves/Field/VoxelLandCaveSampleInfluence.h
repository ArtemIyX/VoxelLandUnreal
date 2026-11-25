#pragma once

#include "CoreMinimal.h"

struct FVoxelLandM5LatticePoint;
struct FVoxelLandM5OwnedLatticeEdge;
struct FVoxelLandM5SampleInfluence;
struct FVoxelLandM5EdgeSamples;
struct FVoxelLandM5PointCm;

// Fixed, conservative inverse of the current LOD0 Surface Nets dependencies.
// Failures preserve the supplied output. No result authorizes publication.
namespace VoxelLand::M5::SampleInfluence
{
[[nodiscard]] VOXELLANDALGO_API bool TryBuild(const FVoxelLandM5LatticePoint& Sample, FVoxelLandM5SampleInfluence& Out);
[[nodiscard]] VOXELLANDALGO_API bool TryToCentimetres(
	const FVoxelLandM5LatticePoint& Point, int64 VoxelSizeCm, FVoxelLandM5PointCm& Out);
[[nodiscard]] VOXELLANDALGO_API bool TryBuildEdgeSamples(
	const FVoxelLandM5OwnedLatticeEdge& Edge, FVoxelLandM5EdgeSamples& Out);
}
