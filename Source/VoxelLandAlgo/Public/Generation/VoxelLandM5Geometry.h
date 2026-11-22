#pragma once

#include "CoreMinimal.h"

// Non-authoritative diagnostic helpers only. Floating-point approximations
// here must not define terrain density, a world identity, or prove capsule
// clearance; those require the versioned integer and final-field contracts.
// These functions reject non-finite or unrepresentable calculations and
// leave Out unchanged on failure.
namespace VoxelLand::M5::Geometry
{
VOXELLANDALGO_API bool TrySphereDistance(FVector3d P, FVector3d Center, double Radius, double& Out);
VOXELLANDALGO_API bool TrySegmentDistance(FVector3d P, FVector3d A, FVector3d B, double Radius, double& Out);
VOXELLANDALGO_API bool TrySphereBounds(FVector3d Center, double Radius, FBox3d& Out);
VOXELLANDALGO_API bool TrySegmentBounds(FVector3d A, FVector3d B, double Radius, FBox3d& Out);
}
