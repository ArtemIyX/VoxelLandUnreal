#pragma once

#include "CoreMinimal.h"

struct FVoxelLandWalkState
{
	FVector3d Center = FVector3d::ZeroVector;
	FVector3d GroundNormal = FVector3d::UpVector;
	double VerticalSpeed = 0.0;
	bool bIsGrounded = false;
};
