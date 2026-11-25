#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Geometry/VoxelLandCaveRouteTransition.h"
#include "Data/Caves/Geometry/VoxelLandCavePointCm.h"
#include "Data/Caves/Geometry/VoxelLandCaveRouteFeatureRef.h"
#include "Data/Caves/Geometry/VoxelLandCaveRouteSupportPlane.h"

// Desired feet motion along a straight segment in logical-world centimetres.
// Neighboring legs must meet exactly or have an independently validated connector.
// A step with bends is expressed as multiple explicit swept legs. This data alone
// asserts neither support nor clearance and must not be treated as validation.
struct FVoxelLandM5RouteLeg
{
	FVoxelLandM5PointCm StartFeetCm;
	FVoxelLandM5PointCm EndFeetCm;
	EVoxelLandM5RouteTransition Transition = EVoxelLandM5RouteTransition::Walk;
	FVoxelLandM5RouteFeatureRef Feature;
	bool bHasSupportPlane = false;
	FVoxelLandM5RouteSupportPlane SupportPlane;
};
